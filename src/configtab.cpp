// configtab.cpp - the Config files tab: review .pacnew and .pacsave files,
// see how they differ from the config in use, and keep or replace.
#include "mainwindow.h"

#include <QDateTime>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSet>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include "widgets.h"

namespace {

enum Column { FileCol, KindCol, DateCol, ColumnCount };

QString qs(const std::string& s) { return QString::fromStdString(s); }

// Files where swapping in the packaged default would wipe your users, disks
// or sudo rights. These have to be merged by hand.
const QSet<QString>& mergeByHand() {
    static const QSet<QString> files = {
        QStringLiteral("/etc/passwd"), QStringLiteral("/etc/shadow"),
        QStringLiteral("/etc/group"),  QStringLiteral("/etc/gshadow"),
        QStringLiteral("/etc/fstab"),  QStringLiteral("/etc/crypttab"),
        QStringLiteral("/etc/sudoers")};
    return files;
}

}  // namespace

QWidget* MainWindow::buildConfigTab() {
    auto* info = new QLabel(tr(
        "When an update ships a new default config but you've changed yours, pacman "
        "keeps yours and saves the new one as <b>.pacnew</b>. When a package is removed, "
        "a config you changed is kept as <b>.pacsave</b>. Pick a file to see how it "
        "differs from the one in use."));
    info->setWordWrap(true);

    m_configTable = new QTableWidget(0, ColumnCount);
    m_configTable->setHorizontalHeaderLabels({tr("File"), tr("Type"), tr("Date")});
    m_configTable->verticalHeader()->hide();
    m_configTable->setAlternatingRowColors(true);
    m_configTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_configTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_configTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_configTable->setWordWrap(false);
    auto* header = m_configTable->horizontalHeader();
    header->setSectionResizeMode(FileCol, QHeaderView::Stretch);
    header->setSectionResizeMode(KindCol, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(DateCol, QHeaderView::ResizeToContents);
    connect(m_configTable, &QTableWidget::itemSelectionChanged, this, [this] {
        showConfigDiff();
        updateConfigButtons();
    });

    m_diffView = new QPlainTextEdit;
    m_diffView->setReadOnly(true);
    m_diffView->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_diffView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_diffView->setPlaceholderText(tr("Select a file to compare it with the config in use."));
    new DiffHighlighter(m_diffView->document());

    auto* legend = new QLabel(tr("<span style='color:#d93f3f'>− lines only in your current "
                                 "file</span> &nbsp; <span style='color:#2ea043'>+ lines only "
                                 "in the other version</span>"));
    auto* diffPane = new QWidget;
    auto* diffLayout = new QVBoxLayout(diffPane);
    diffLayout->setContentsMargins(0, 0, 0, 0);
    diffLayout->addWidget(legend);
    diffLayout->addWidget(m_diffView, 1);

    auto* split = new QSplitter(Qt::Horizontal);
    split->addWidget(m_configTable);
    split->addWidget(diffPane);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    split->setSizes({480, 700});

    m_configStatus = new QLabel;
    m_keepCurrentBtn = makeButton(tr("Keep current version"), [this] { keepCurrentConfigs(); });
    m_keepCurrentBtn->setToolTip(tr("Delete the .pacnew/.pacsave file. Your config in use doesn't change."));
    m_useOtherBtn = makeButton(tr("Use this version"), [this] { useOtherConfigs(); });
    m_useOtherBtn->setToolTip(tr("Replace the config in use with this file. "
                                 "The current one is backed up with .bak added to its name."));

    auto* bottom = new QHBoxLayout;
    bottom->addWidget(m_configStatus, 1);
    bottom->addWidget(makeButton(tr("Rescan"), [this] { scanConfigs(); }));
    bottom->addWidget(m_keepCurrentBtn);
    bottom->addWidget(m_useOtherBtn);

    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(info);
    layout->addWidget(split, 1);
    layout->addLayout(bottom);
    return page;
}

void MainWindow::scanConfigs() {
    m_configStatus->setText(tr("Looking for .pacnew and .pacsave files…"));
    inBackground([] { return core::findConfigFiles(); },
                 [this](const std::vector<core::ConfigFile>& files) {
                     m_configs = files;
                     populateConfigs();
                 });
}

void MainWindow::populateConfigs() {
    m_configTable->blockSignals(true);
    m_configTable->setSortingEnabled(false);
    m_configTable->setRowCount(0);
    m_configTable->setRowCount(static_cast<int>(m_configs.size()));
    const Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    for (int i = 0; i < static_cast<int>(m_configs.size()); ++i) {
        const auto& c = m_configs[static_cast<size_t>(i)];
        auto* file = new QTableWidgetItem(qs(c.path));
        file->setData(Qt::UserRole, i);
        file->setToolTip(qs(c.path));
        auto* kind = new QTableWidgetItem(c.kind == "pacnew" ? tr("New default (.pacnew)")
                                                             : tr("Saved old (.pacsave)"));
        auto* date = new NumericItem(
            QDateTime::fromSecsSinceEpoch(c.mtime).toString(QStringLiteral("yyyy-MM-dd")), c.mtime);
        for (auto* item : {file, kind, static_cast<QTableWidgetItem*>(date)}) item->setFlags(flags);
        m_configTable->setItem(i, FileCol, file);
        m_configTable->setItem(i, KindCol, kind);
        m_configTable->setItem(i, DateCol, date);
    }
    m_configTable->setSortingEnabled(true);
    m_configTable->sortItems(FileCol, Qt::AscendingOrder);
    m_configTable->blockSignals(false);

    const int n = static_cast<int>(m_configs.size());
    setTabCount(ConfigTab, tr("Config files"), n);
    m_configStatus->setText(n ? tr("%1 files to review").arg(n)
                              : tr("Nothing to review. Your config files are up to date."));
    m_diffView->clear();
    if (n) m_configTable->selectRow(0);
    updateConfigButtons();
}

QList<int> MainWindow::selectedConfigs() const {
    QList<int> out;
    if (!m_configTable) return out;
    for (const auto& index : m_configTable->selectionModel()->selectedRows())
        if (auto* item = m_configTable->item(index.row(), FileCol))
            out << item->data(Qt::UserRole).toInt();
    return out;
}

void MainWindow::showConfigDiff() {
    const int row = m_configTable->currentRow();
    if (row < 0 || !m_configTable->item(row, FileCol)) {
        m_diffView->clear();
        return;
    }
    const auto c = m_configs[static_cast<size_t>(m_configTable->item(row, FileCol)->data(Qt::UserRole).toInt())];
    if (!c.readable) {
        m_diffView->setPlainText(tr(
            "You don't have permission to read this file, so the differences can't be "
            "shown here.\n\nYou can still keep your current version or switch to this one."));
        return;
    }
    const QString path = qs(c.path);
    m_diffView->setProperty("path", path);
    m_diffView->setPlainText(tr("Comparing…"));
    inBackground([c] { return core::diffFiles(c.original, c.path); },
                 [this, path, c](const std::string& diff) {
                     if (m_diffView->property("path").toString() != path) return;
                     if (!diff.empty())
                         m_diffView->setPlainText(qs(diff));
                     else if (c.originalExists)
                         m_diffView->setPlainText(tr("The two files are identical, so it's safe "
                                                     "to keep your current version."));
                     else
                         m_diffView->setPlainText(tr("There's no file in use at %1 to compare with.")
                                                      .arg(qs(c.original)));
                 });
}

void MainWindow::updateConfigButtons() {
    if (!m_keepCurrentBtn || !m_useOtherBtn) return;
    const auto sel = selectedConfigs();
    bool allPacsave = !sel.isEmpty(), allPacnew = !sel.isEmpty();
    for (int i : sel) {
        const bool pacnew = m_configs[static_cast<size_t>(i)].kind == "pacnew";
        allPacsave = allPacsave && !pacnew;
        allPacnew = allPacnew && pacnew;
    }
    m_keepCurrentBtn->setText(allPacsave ? tr("Delete saved copy") : tr("Keep current version"));
    m_useOtherBtn->setText(allPacsave ? tr("Restore saved copy")
                                      : allPacnew ? tr("Use new version") : tr("Use this version"));
    const bool enabled = !sel.isEmpty() && !m_proc;
    m_keepCurrentBtn->setEnabled(enabled);
    m_useOtherBtn->setEnabled(enabled);
}

void MainWindow::keepCurrentConfigs() {
    const auto sel = selectedConfigs();
    if (sel.isEmpty()) return;
    QStringList paths;
    for (int i : sel) paths << qs(m_configs[static_cast<size_t>(i)].path);
    if (!confirm(tr("Keep current version"),
                 tr("Delete these files? The config files in use stay exactly as they are.\n\n%1")
                     .arg(paths.join('\n'))))
        return;
    runCommand(QStringList{QStringLiteral("rm"), QStringLiteral("-f"), QStringLiteral("--")} + paths,
               true, [this](int) { scanConfigs(); });
}

void MainWindow::useOtherConfigs() {
    const auto sel = selectedConfigs();
    if (sel.isEmpty()) return;

    QStringList blocked;
    for (int i : sel) {
        const auto& c = m_configs[static_cast<size_t>(i)];
        if (mergeByHand().contains(qs(c.original))) blocked << qs(c.original);
    }
    if (!blocked.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr(
            "These files hold your user accounts, disks or sudo rights:\n\n%1\n\n"
            "Replacing them with the packaged default would wipe those settings, so "
            "they need to be merged by hand. In a terminal, run: sudo pacdiff")
            .arg(blocked.join('\n')));
        return;
    }

    // Each pair: the .pacnew/.pacsave file, then the config it replaces.
    static const QString script = QStringLiteral(
        "set -e; while [ $# -ge 2 ]; do "
        "if [ -e \"$2\" ]; then cp -a -- \"$2\" \"$2.bak\"; fi; "
        "mv -f -- \"$1\" \"$2\"; shift 2; done");
    QStringList args{QStringLiteral("sh"), QStringLiteral("-c"), script, QStringLiteral("sh")};
    QStringList listing;
    for (int i : sel) {
        const auto& c = m_configs[static_cast<size_t>(i)];
        args << qs(c.path) << qs(c.original);
        listing << tr("%1  →  %2").arg(qs(c.path), qs(c.original));
    }
    if (!confirm(tr("Replace config files"),
                 tr("Replace these config files?\n\n%1\n\nEach file in use is first backed up "
                    "next to itself with .bak added to its name, so you can switch back.")
                     .arg(listing.join('\n'))))
        return;
    runCommand(args, true, [this](int) { scanConfigs(); });
}
