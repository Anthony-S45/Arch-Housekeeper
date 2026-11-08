// mainwindow.cpp - setup, shared helpers, and the Applications, Orphans and
// Cache tabs. See mainwindow.h for where the other tabs live.
#include "mainwindow.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

#include <filesystem>
#include <unistd.h>

#include "packagetable.h"

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }
QString humanQ(quint64 bytes) { return qs(core::human(bytes)); }

}  // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("Arch Housekeeper"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("system-software-update")));
    resize(1250, 800);
    m_hasPaccache = !QStandardPaths::findExecutable(QStringLiteral("paccache")).isEmpty();
    loadKept();

    m_tabs = new QTabWidget;
    m_tabs->insertTab(AppsTab, buildAppsTab(), tr("Applications"));
    m_tabs->insertTab(OrphansTab, buildOrphansTab(), tr("Orphans"));
    m_tabs->insertTab(ConfigTab, buildConfigTab(), tr("Config files"));
    m_tabs->insertTab(LeftoversTab, buildLeftoversTab(), tr("Leftovers"));
    m_tabs->insertTab(CacheTab, buildCacheTab(), tr("Cache"));

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setPlaceholderText(tr("Command output appears here."));
    m_log->setMaximumBlockCount(20000);

    auto* splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(m_tabs);
    splitter->addWidget(m_log);
    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({600, 160});
    setCentralWidget(splitter);

    setBusy(false);
    QTimer::singleShot(0, this, &MainWindow::refreshAll);
}

QPushButton* MainWindow::makeButton(const QString& text, std::function<void()> slot,
                                    bool action) {
    auto* button = new QPushButton(text);
    connect(button, &QPushButton::clicked, this, [slot = std::move(slot)] { slot(); });
    if (action) m_actionButtons.append(button);
    return button;
}

QWidget* MainWindow::buildAppsTab() {
    m_apps = new PackageTable;
    m_appsDetails = makeDetailsBrowser();
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(tr("Filter by name or description…"));
    m_filter->setClearButtonEnabled(true);
    connect(m_filter, &QLineEdit::textChanged, m_apps, &PackageTable::applyFilter);

    m_showSystem = new QCheckBox(tr("Include base system packages"));
    connect(m_showSystem, &QCheckBox::toggled, this, [this] { scanApps(); });
    m_removeDeps = new QCheckBox(tr("Also remove dependencies nothing else needs"));
    m_removeDeps->setChecked(true);

    m_appsStatus = new QLabel;
    connect(m_apps, &PackageTable::checksChanged, this,
            [this] { updateStatus(m_apps, m_appsStatus); });

    auto* top = new QHBoxLayout;
    top->addWidget(m_filter, 1);
    top->addWidget(m_showSystem);
    top->addWidget(makeButton(tr("Rescan"), [this] { scanApps(); }));
    top->addWidget(makeButton(tr("Export list to Desktop"), [this] { exportApps(); }, false));

    auto* bottom = new QHBoxLayout;
    bottom->addWidget(m_appsStatus, 1);
    bottom->addWidget(m_removeDeps);
    bottom->addWidget(makeButton(tr("Select all"), [this] { m_apps->setAllChecked(true); }, false));
    bottom->addWidget(makeButton(tr("Select none"), [this] { m_apps->setAllChecked(false); }, false));
    bottom->addWidget(makeButton(tr("Remove selected…"), [this] {
        removePackages(m_apps, m_removeDeps->isChecked() ? "-Rns" : "-Rn", tr("applications"));
    }));

    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->addLayout(top);
    layout->addWidget(withDetails(m_apps, m_appsDetails), 1);
    layout->addLayout(bottom);
    return page;
}

QWidget* MainWindow::buildOrphansTab() {
    m_orphans = new PackageTable;
    m_orphansDetails = makeDetailsBrowser();
    m_orphansStatus = new QLabel;
    connect(m_orphans, &PackageTable::checksChanged, this,
            [this] { updateStatus(m_orphans, m_orphansStatus); });

    auto* info = new QLabel(tr(
        "Orphans are packages that were installed as dependencies but nothing "
        "needs anymore. Most are safe to remove. If you use one directly (an "
        "optional dependency, say), keep it instead and it won't show up here again."));
    info->setWordWrap(true);

    auto* bottom = new QHBoxLayout;
    bottom->addWidget(m_orphansStatus, 1);
    bottom->addWidget(makeButton(tr("Rescan"), [this] { scanOrphans(); }));
    bottom->addWidget(makeButton(tr("Select all"), [this] { m_orphans->setAllChecked(true); }, false));
    bottom->addWidget(makeButton(tr("Select none"), [this] { m_orphans->setAllChecked(false); }, false));
    bottom->addWidget(makeButton(tr("Keep selected"), [this] { keepOrphans(); }));
    bottom->addWidget(makeButton(tr("Remove selected…"), [this] {
        removePackages(m_orphans, QStringLiteral("-Rns"), tr("orphans"));
    }));

    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(info);
    layout->addWidget(withDetails(m_orphans, m_orphansDetails), 1);
    layout->addLayout(bottom);
    return page;
}

QWidget* MainWindow::buildCacheTab() {
    auto paccacheButton = [this](const QString& text, std::function<void()> slot) {
        auto* b = makeButton(text, std::move(slot));
        m_paccacheButtons.append(b);
        return b;
    };

    auto* box = new QGroupBox(tr("Pacman package cache"));
    auto* b = new QVBoxLayout(box);
    m_cacheLabel = new QLabel(tr("Scanning…"));
    m_cacheLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    b->addWidget(m_cacheLabel);
    auto* explain = new QLabel(tr(
        "Pacman keeps every version it has ever downloaded. Keeping a few recent "
        "ones lets you downgrade offline if an update breaks something."));
    explain->setWordWrap(true);
    b->addWidget(explain);

    m_keepSpin = new QSpinBox;
    m_keepSpin->setRange(0, 10);
    m_keepSpin->setValue(3);
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Keep the last")));
    row->addWidget(m_keepSpin);
    row->addWidget(new QLabel(tr("versions of each package")));
    row->addStretch(1);
    row->addWidget(paccacheButton(tr("Preview"), [this] {
        paccache({"-k", QString::number(m_keepSpin->value())}, true);
    }));
    row->addWidget(paccacheButton(tr("Clean"), [this] {
        paccache({"-k", QString::number(m_keepSpin->value())}, false);
    }));
    b->addLayout(row);

    row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Remove cached files for software that's no longer installed")));
    row->addStretch(1);
    row->addWidget(paccacheButton(tr("Preview"), [this] { paccache({"-u", "-k", "0"}, true); }));
    row->addWidget(paccacheButton(tr("Clean"), [this] { paccache({"-u", "-k", "0"}, false); }));
    b->addLayout(row);

    row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Empty the whole cache (no offline downgrades afterwards)")));
    row->addStretch(1);
    row->addWidget(paccacheButton(tr("Empty cache…"), [this] { emptyCache(); }));
    b->addLayout(row);

    row = new QHBoxLayout;
    m_timerLabel = new QLabel;
    row->addWidget(m_timerLabel);
    row->addStretch(1);
    m_timerButton = paccacheButton(tr("Enable weekly auto-clean"), [this] { enableTimer(); });
    row->addWidget(m_timerButton);
    b->addLayout(row);

    if (!m_hasPaccache) {
        auto* warn = new QLabel(tr("<b>paccache isn't installed.</b> Install it with "
                                   "<code>sudo pacman -S pacman-contrib</code> to "
                                   "enable these options."));
        warn->setWordWrap(true);
        b->addWidget(warn);
    }

    m_aurBox = new QGroupBox(tr("AUR helper build cache"));
    new QVBoxLayout(m_aurBox);

    auto* refresh = new QHBoxLayout;
    refresh->addStretch(1);
    refresh->addWidget(makeButton(tr("Refresh sizes"), [this] { scanCache(); }));

    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(box);
    layout->addWidget(m_aurBox);
    layout->addLayout(refresh);
    layout->addStretch(1);
    return page;
}

// ---------------------------------------------------------------------------
// Running commands
// ---------------------------------------------------------------------------

void MainWindow::runCommand(const QStringList& args, bool root,
                            std::function<void(int)> onDone) {
    if (m_proc || args.isEmpty()) return;

    QString program;
    QStringList argv;
    if (root && geteuid() != 0) {
        if (QStandardPaths::findExecutable(QStringLiteral("pkexec")).isEmpty()) {
            QMessageBox::critical(this, windowTitle(),
                                  tr("pkexec (polkit) is needed to run this as root."));
            return;
        }
        program = QStringLiteral("pkexec");
        const QString exe = QStandardPaths::findExecutable(args.first());
        argv << (exe.isEmpty() ? args.first() : exe) << args.mid(1);
    } else {
        program = args.first();
        argv = args.mid(1);
    }

    logLine(QString(root ? "# " : "$ ") + args.join(' '));
    auto* proc = new QProcess(this);
    m_proc = proc;
    proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc] {
        appendRaw(QString::fromUtf8(proc->readAllStandardOutput()));
    });
    connect(proc, &QProcess::finished, this,
            [this, root, onDone](int code, QProcess::ExitStatus status) {
                commandFinished(status == QProcess::CrashExit ? -2 : code, root, onDone);
            });
    connect(proc, &QProcess::errorOccurred, this,
            [this, root, onDone](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) commandFinished(-1, root, onDone);
            });
    setBusy(true);
    proc->start(program, argv);
}

void MainWindow::commandFinished(int code, bool root, const std::function<void(int)>& onDone) {
    if (!m_proc) return;
    m_proc->deleteLater();
    m_proc = nullptr;
    setBusy(false);

    if (root && (code == 126 || code == 127))
        logLine(tr("Cancelled: authentication was dismissed or denied."));
    else if (code == -1)
        logLine(tr("Couldn't start the command."));
    else if (code == -2)
        logLine(tr("The command crashed."));
    else
        logLine(tr("Finished (exit code %1).").arg(code));

    if (onDone) onDone(code);
}

void MainWindow::setBusy(bool busy) {
    for (const auto& b : m_actionButtons)
        if (b) b->setEnabled(!busy);
    if (!m_hasPaccache)
        for (const auto& b : m_paccacheButtons)
            if (b) b->setEnabled(false);
    if (!busy) {
        updateConfigButtons();
        updateLeftoverStatus();
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void MainWindow::logLine(const QString& text) {
    if (!m_logEndsWithNewline) appendRaw(QStringLiteral("\n"));
    appendRaw(text + '\n');
}

void MainWindow::appendRaw(const QString& text) {
    if (text.isEmpty()) return;
    m_log->moveCursor(QTextCursor::End);
    m_log->insertPlainText(text);
    m_log->ensureCursorVisible();
    m_logEndsWithNewline = text.endsWith('\n');
}

bool MainWindow::confirm(const QString& title, const QString& text) {
    return QMessageBox::question(this, title, text, QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No) == QMessageBox::Yes;
}

bool MainWindow::pacmanLocked() {
    if (!QFileInfo::exists(QStringLiteral("/var/lib/pacman/db.lck"))) return false;
    QMessageBox::warning(this, windowTitle(), tr(
        "Another pacman process is running (the database is locked). Wait for it "
        "to finish and try again.\n\nIf nothing is running, a crash may have left "
        "the lock behind: /var/lib/pacman/db.lck"));
    return true;
}

void MainWindow::updateStatus(PackageTable* table, QLabel* label) {
    const int n = table->checked().size();
    QString text = tr("%1 packages").arg(table->rowCount());
    if (n) text += tr(" · %1 selected (%2)").arg(n).arg(humanQ(table->checkedSize()));
    label->setText(text);
}

void MainWindow::setTabCount(Tab tab, const QString& name, int count) {
    m_tabs->setTabText(tab, count > 0 ? tr("%1 (%2)").arg(name).arg(count) : name);
}

// ---------------------------------------------------------------------------
// Scanning
// ---------------------------------------------------------------------------

void MainWindow::refreshAll() {
    scanApps();
    scanOrphans();
    scanConfigs();
    scanLeftovers();
    scanCache();
}

void MainWindow::scanApps() {
    m_appsStatus->setText(tr("Scanning installed packages…"));
    const bool includeSystem = m_showSystem->isChecked();
    inBackground([includeSystem] { return core::findApps(includeSystem); },
                 [this](const std::vector<core::Package>& pkgs) { m_apps->setPackages(pkgs); });
}

void MainWindow::scanOrphans() {
    m_orphansStatus->setText(tr("Looking for orphans…"));
    inBackground([] { return core::findOrphans(); },
                 [this](const std::vector<core::Package>& pkgs) {
                     m_orphans->setPackages(pkgs);
                     setTabCount(OrphansTab, tr("Orphans"), static_cast<int>(pkgs.size()));
                     if (pkgs.empty()) m_orphansStatus->setText(tr("No orphans. Nice and tidy."));
                 });
}

void MainWindow::scanCache() {
    m_cacheLabel->setText(tr("Scanning…"));
    inBackground([] { return core::scanCache(); },
                 [this](const core::CacheInfo& info) { showCache(info); });
}

void MainWindow::showCache(const core::CacheInfo& info) {
    QStringList lines;
    for (const auto& d : info.dirs)
        lines << tr("<b>%1</b> in %2 package files &nbsp;<span style='color:gray'>%3</span>")
                     .arg(humanQ(d.size))
                     .arg(d.count)
                     .arg(qs(d.path).toHtmlEscaped());
    m_cacheLabel->setText(lines.isEmpty() ? tr("No cache directory found.")
                                          : lines.join(QStringLiteral("<br>")));

    m_timerLabel->setText(tr("Weekly auto-clean (paccache.timer, keeps the last 3): %1")
                              .arg(info.timerEnabled ? tr("<b>on</b>") : tr("off")));
    m_timerButton->setVisible(!info.timerEnabled);

    // Rebuild the AUR rows. Buttons are held by QPointer, so the deleted
    // ones simply become null in the button lists.
    delete m_aurContent;
    m_aurContent = new QWidget;
    auto* layout = new QVBoxLayout(m_aurContent);
    layout->setContentsMargins(0, 0, 0, 0);
    if (info.aur.empty()) layout->addWidget(new QLabel(tr("No yay or paru cache found.")));
    for (const auto& a : info.aur) {
        const QString name = qs(a.name), path = qs(a.path);
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("<b>%1</b>: %2 &nbsp;<span style='color:gray'>%3</span>")
                                      .arg(name, humanQ(a.size), path.toHtmlEscaped())),
                       1);
        row->addWidget(makeButton(tr("Clear…"), [this, name, path] { clearAur(name, path); }));
        layout->addLayout(row);
    }
    m_aurBox->layout()->addWidget(m_aurContent);
    m_actionButtons.removeAll(nullptr);
    setBusy(m_proc != nullptr);
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void MainWindow::exportApps() {
    const auto& apps = m_apps->packages();
    if (apps.empty()) return;
    const QString path = qs(core::exportList(apps));
    if (path.isEmpty()) {
        QMessageBox::critical(this, windowTitle(), tr("Couldn't save the list to your Desktop."));
        return;
    }
    logLine(tr("Saved %1 applications to %2").arg(apps.size()).arg(path));
    statusBar()->showMessage(tr("Saved to %1").arg(path), 8000);
}

void MainWindow::removePackages(PackageTable* table, const QString& flags, const QString& what) {
    const QStringList names = table->checked();
    if (names.isEmpty()) {
        QMessageBox::information(this, windowTitle(),
                                 tr("Tick the %1 you want to remove first.").arg(what));
        return;
    }
    if (pacmanLocked()) return;

    QString listing = names.mid(0, 40).join(QStringLiteral(", "));
    if (names.size() > 40) listing += tr(" … and %1 more").arg(names.size() - 40);
    QString text = tr("Remove %1 %2 (about %3)?\n\n%4")
                       .arg(names.size())
                       .arg(what, humanQ(table->checkedSize()), listing);

    QStringList risky;
    for (const auto& n : names)
        if (core::isProtected(n.toStdString())) risky << n;
    if (!risky.isEmpty())
        text += tr("\n\n⚠ These look important to your system: %1\nRemoving them could "
                   "leave it unbootable or without a desktop.")
                    .arg(risky.join(QStringLiteral(", ")));

    if (!confirm(tr("Remove %1").arg(what), text)) return;

    runCommand(QStringList{QStringLiteral("pacman"), flags, QStringLiteral("--noconfirm")} + names,
               true, [this](int code) {
                   if (code > 0 && code != 126 && code != 127)
                       QMessageBox::warning(this, windowTitle(), tr(
                           "pacman didn't remove the packages. Usually this means another "
                           "package still depends on one of them. The log below says which."));
                   refreshAll();
               });
}

void MainWindow::keepOrphans() {
    const QStringList names = m_orphans->checked();
    if (names.isEmpty() || pacmanLocked()) return;
    runCommand(QStringList{QStringLiteral("pacman"), QStringLiteral("-D"),
                           QStringLiteral("--asexplicit")} + names,
               true, [this](int) { refreshAll(); });
}

void MainWindow::paccache(const QStringList& options, bool dryRun) {
    if (!dryRun && pacmanLocked()) return;
    QStringList args{QStringLiteral("paccache"), dryRun ? QStringLiteral("-d") : QStringLiteral("-r")};
    args << options;
    for (const auto& d : core::cacheDirs()) args << QStringLiteral("-c") << qs(d);
    // A dry run only reads the cache, so it doesn't need root.
    runCommand(args, !dryRun, dryRun ? std::function<void(int)>{} : [this](int) { scanCache(); });
}

void MainWindow::emptyCache() {
    if (confirm(tr("Empty package cache"),
                tr("Delete every cached package file?\n\nYou won't be able to downgrade a "
                   "package without downloading the old version again.")))
        paccache({QStringLiteral("-k"), QStringLiteral("0")}, false);
}

void MainWindow::enableTimer() {
    runCommand({QStringLiteral("systemctl"), QStringLiteral("enable"), QStringLiteral("--now"),
                QStringLiteral("paccache.timer")},
               true, [this](int) { scanCache(); });
}

void MainWindow::clearAur(const QString& name, const QString& path) {
    if (!confirm(tr("Clear %1 cache").arg(name),
                 tr("Delete %1?\n\n%2 will download build files again the next time it "
                    "needs them.").arg(path, name)))
        return;
    logLine(QStringLiteral("$ rm -rf ") + path);
    const std::string p = path.toStdString();
    inBackground(
        [p] {
            std::error_code ec;
            std::filesystem::remove_all(p, ec);
            return ec ? ec.message() : std::string{};
        },
        [this, path](const std::string& error) {
            logLine(error.empty() ? tr("Cleared %1").arg(path)
                                  : tr("Couldn't fully clear %1: %2").arg(path, qs(error)));
            scanCache();
        });
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_proc) {
        QMessageBox::warning(this, windowTitle(), tr(
            "A command is still running. Closing now could interrupt pacman "
            "mid-transaction, so please wait for it to finish."));
        event->ignore();
        return;
    }
    for (const auto& t : m_threads)
        if (t) t->wait(5000);
    event->accept();
}
