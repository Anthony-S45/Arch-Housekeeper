// leftoverstab.cpp - the Leftovers tab: folders apps leave behind. Nothing is
// removed unless it's ticked and confirmed. Home folders go to the Trash.
#include "mainwindow.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

#include "widgets.h"

namespace {

enum Column { CheckCol, FolderCol, WhereCol, VerdictCol, SizeCol, UsedCol, ReasonCol, ColumnCount };

QString qs(const std::string& s) { return QString::fromStdString(s); }
QString humanQ(quint64 bytes) { return qs(core::human(bytes)); }

QString shortPath(const QString& path) {
    const QString home = QDir::homePath();
    return path.startsWith(home + '/') ? QStringLiteral("~") + path.mid(home.size()) : path;
}

QString dateText(std::int64_t secs) {
    return secs > 0 ? QDateTime::fromSecsSinceEpoch(secs).toString(QStringLiteral("yyyy-MM-dd"))
                    : QStringLiteral("—");
}

}  // namespace

QWidget* MainWindow::buildLeftoversTab() {
    auto* info = new QLabel(tr(
        "Folders that apps leave behind in your home folder and in system locations. "
        "Pick one to see what's inside. Nothing is removed unless you tick it and "
        "confirm, and folders in your home folder go to the Trash so you can restore them."));
    info->setWordWrap(true);

    m_showInUse = new QCheckBox(tr("Show folders that are in use"));
    m_showKept = new QCheckBox(tr("Show folders you're keeping"));
    connect(m_showInUse, &QCheckBox::toggled, this, [this] { populateLeftovers(); });
    connect(m_showKept, &QCheckBox::toggled, this, [this] { populateLeftovers(); });

    auto* top = new QHBoxLayout;
    top->addWidget(m_showInUse);
    top->addWidget(m_showKept);
    top->addStretch(1);
    top->addWidget(makeButton(tr("Rescan"), [this] { scanLeftovers(); }));

    m_leftTable = new QTableWidget(0, ColumnCount);
    m_leftTable->setHorizontalHeaderLabels({QString(), tr("Folder"), tr("Where"), tr("Verdict"),
                                            tr("Size"), tr("Last changed"), tr("Why")});
    m_leftTable->verticalHeader()->hide();
    m_leftTable->setAlternatingRowColors(true);
    m_leftTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_leftTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_leftTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_leftTable->setWordWrap(false);
    auto* header = m_leftTable->horizontalHeader();
    for (int col = 0; col < ColumnCount; ++col)
        header->setSectionResizeMode(col, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(FolderCol, QHeaderView::Interactive);
    header->setSectionResizeMode(ReasonCol, QHeaderView::Stretch);
    m_leftTable->setColumnWidth(FolderCol, 280);
    connect(m_leftTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (item->column() == CheckCol) updateLeftoverStatus();
    });
    connect(m_leftTable, &QTableWidget::currentCellChanged, this,
            [this](int, int, int, int) { showLeftoverDetails(); });

    m_leftDetails = new QTextBrowser;
    m_leftDetails->setOpenLinks(false);
    m_openFolderBtn = makeButton(tr("Open in file manager"), [this] {
        const int row = m_leftTable->currentRow();
        if (row < 0) return;
        const int i = m_leftTable->item(row, CheckCol)->data(Qt::UserRole).toInt();
        QDesktopServices::openUrl(QUrl::fromLocalFile(qs(m_leftovers[static_cast<size_t>(i)].path)));
    }, false);
    auto* side = new QWidget;
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->addWidget(m_leftDetails, 1);
    sideLayout->addWidget(m_openFolderBtn);

    auto* split = new QSplitter(Qt::Horizontal);
    split->addWidget(m_leftTable);
    split->addWidget(side);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    split->setSizes({800, 400});

    m_leftStatus = new QLabel;
    m_keepBtn = makeButton(tr("Keep selected"), [this] { keepLeftovers(true); }, false);
    m_keepBtn->setToolTip(tr("Hide these folders from future scans. "
                             "Tick “Show folders you're keeping” to see them again."));
    m_unkeepBtn = makeButton(tr("Stop keeping"), [this] { keepLeftovers(false); }, false);

    auto* bottom = new QHBoxLayout;
    bottom->addWidget(m_leftStatus, 1);
    bottom->addWidget(makeButton(tr("Select likely leftovers"), [this] { setLeftoverChecks(true, true); }, false));
    bottom->addWidget(makeButton(tr("Select none"), [this] { setLeftoverChecks(false, false); }, false));
    bottom->addWidget(m_keepBtn);
    bottom->addWidget(m_unkeepBtn);
    bottom->addWidget(makeButton(tr("Remove selected…"), [this] { removeLeftovers(); }));

    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(info);
    layout->addLayout(top);
    layout->addWidget(split, 1);
    layout->addLayout(bottom);
    showLeftoverDetails();
    return page;
}

void MainWindow::loadKept() {
    m_kept = QSettings().value(QStringLiteral("leftovers/kept")).toStringList();
}

void MainWindow::saveKept() {
    QSettings().setValue(QStringLiteral("leftovers/kept"), m_kept);
}

void MainWindow::scanLeftovers() {
    m_leftStatus->setText(tr("Scanning folders… this can take a minute on a big home folder."));
    inBackground([] { return core::findLeftovers(); },
                 [this](const std::vector<core::Leftover>& found) {
                     m_leftovers = found;
                     populateLeftovers();
                 });
}

void MainWindow::populateLeftovers() {
    m_leftTable->blockSignals(true);
    m_leftTable->setSortingEnabled(false);
    m_leftTable->setRowCount(0);

    const Qt::ItemFlags readOnly = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    int row = 0, likely = 0;
    for (int i = 0; i < static_cast<int>(m_leftovers.size()); ++i) {
        const auto& l = m_leftovers[static_cast<size_t>(i)];
        const QString path = qs(l.path);
        const bool kept = m_kept.contains(path);
        const bool inUse = l.verdict == core::Verdict::InUse;
        if (inUse && !m_showInUse->isChecked()) continue;
        if (kept && !m_showKept->isChecked()) continue;
        if (l.verdict == core::Verdict::Likely && !kept) ++likely;

        auto* check = new QTableWidgetItem;
        check->setFlags(inUse ? readOnly : readOnly | Qt::ItemIsUserCheckable);
        if (!inUse) check->setCheckState(Qt::Unchecked);
        check->setData(Qt::UserRole, i);

        auto* folder = new QTableWidgetItem(shortPath(path));
        folder->setToolTip(path);

        QString verdict;
        int rank = 0;
        QColor color;
        if (kept) {
            verdict = tr("Keeping");
            rank = 2;
        } else if (l.verdict == core::Verdict::Likely) {
            verdict = tr("Likely leftover");
            color = QColor(0xd9, 0x7a, 0x1a);
        } else if (l.verdict == core::Verdict::Possibly) {
            verdict = tr("Possibly leftover");
            rank = 1;
        } else {
            verdict = tr("In use");
            rank = 3;
            color = Qt::gray;
        }
        auto* verdictItem = new NumericItem(verdict, rank);
        if (color.isValid()) verdictItem->setForeground(color);

        auto* size = inUse ? new NumericItem(QStringLiteral("—"), -1)
                           : new NumericItem(humanQ(l.size) + (l.partial ? QStringLiteral("+") : QString()),
                                             static_cast<qint64>(l.size));
        size->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        if (l.partial) size->setToolTip(tr("Some parts need root to read, so it may be larger."));

        QTableWidgetItem* cells[ColumnCount] = {
            check,
            folder,
            new QTableWidgetItem(l.system ? tr("System") : tr("Home")),
            verdictItem,
            size,
            new NumericItem(dateText(l.lastUsed), l.lastUsed),
            new QTableWidgetItem(qs(l.reason)),
        };
        m_leftTable->insertRow(row);
        for (int col = 0; col < ColumnCount; ++col) {
            if (col != CheckCol) cells[col]->setFlags(readOnly);
            m_leftTable->setItem(row, col, cells[col]);
        }
        ++row;
    }
    m_leftTable->setSortingEnabled(true);
    m_leftTable->sortItems(VerdictCol, Qt::AscendingOrder);
    m_leftTable->blockSignals(false);

    setTabCount(LeftoversTab, tr("Leftovers"), likely);
    if (row) m_leftTable->selectRow(0);
    showLeftoverDetails();
    updateLeftoverStatus();
}

QList<int> MainWindow::checkedLeftovers() const {
    QList<int> out;
    if (!m_leftTable) return out;
    for (int r = 0; r < m_leftTable->rowCount(); ++r) {
        const auto* item = m_leftTable->item(r, CheckCol);
        if (item && (item->flags() & Qt::ItemIsUserCheckable) && item->checkState() == Qt::Checked)
            out << item->data(Qt::UserRole).toInt();
    }
    return out;
}

void MainWindow::setLeftoverChecks(bool likelyOnly, bool checked) {
    m_leftTable->blockSignals(true);
    for (int r = 0; r < m_leftTable->rowCount(); ++r) {
        auto* item = m_leftTable->item(r, CheckCol);
        if (!(item->flags() & Qt::ItemIsUserCheckable)) continue;
        const auto& l = m_leftovers[static_cast<size_t>(item->data(Qt::UserRole).toInt())];
        bool state = checked;
        if (likelyOnly)
            state = l.verdict == core::Verdict::Likely && !m_kept.contains(qs(l.path));
        item->setCheckState(state ? Qt::Checked : Qt::Unchecked);
    }
    m_leftTable->blockSignals(false);
    updateLeftoverStatus();
}

void MainWindow::updateLeftoverStatus() {
    if (!m_leftStatus || !m_leftTable) return;
    const auto checked = checkedLeftovers();
    quint64 total = 0;
    for (int i : checked) total += m_leftovers[static_cast<size_t>(i)].size;
    QString text = tr("%1 folders shown").arg(m_leftTable->rowCount());
    if (!checked.isEmpty())
        text += tr(" · %1 selected (%2)").arg(checked.size()).arg(humanQ(total));
    if (!m_leftovers.empty() || m_leftTable->rowCount()) m_leftStatus->setText(text);
    m_unkeepBtn->setEnabled(m_showKept->isChecked());
}

void MainWindow::showLeftoverDetails() {
    const int row = m_leftTable->currentRow();
    const auto* item = row >= 0 ? m_leftTable->item(row, CheckCol) : nullptr;
    m_openFolderBtn->setEnabled(item != nullptr);
    if (!item) {
        m_leftDetails->setHtml(tr("<p style='color:gray'>Select a folder to see what's "
                                  "inside it and why it's listed.</p>"));
        return;
    }
    const auto& l = m_leftovers[static_cast<size_t>(item->data(Qt::UserRole).toInt())];
    const QString path = qs(l.path);
    const QFileInfo fi(path);

    QString h = QStringLiteral("<h3>%1</h3><p><code>%2</code></p>")
                    .arg(fi.fileName().toHtmlEscaped(), path.toHtmlEscaped());
    if (m_kept.contains(path)) h += tr("<p><b>You chose to keep this folder.</b></p>");

    const QString verdict = l.verdict == core::Verdict::Likely   ? tr("Likely leftover")
                            : l.verdict == core::Verdict::Possibly ? tr("Possibly leftover")
                                                                   : tr("In use");
    h += QStringLiteral("<p><b>%1:</b> %2</p>").arg(verdict, qs(l.reason).toHtmlEscaped());
    if (l.verdict != core::Verdict::InUse) {
        h += tr("<p>Size: %1").arg(humanQ(l.size));
        if (l.partial) h += tr(" (some parts need root to read, so it may be larger)");
        h += QStringLiteral("</p>");
    }
    h += tr("<p>Last changed: %1</p>").arg(dateText(l.lastUsed));
    if (l.system)
        h += tr("<p style='color:gray'>This is a system folder. Removing it needs your "
                "password, and it's deleted permanently rather than moved to the Trash.</p>");

    h += QStringLiteral("<h4>%1</h4>").arg(tr("Contents"));
    const QDir dir(path);
    if (!dir.isReadable()) {
        h += tr("<p>Can't look inside without root.</p>");
    } else {
        const auto entries = dir.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
            QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
        if (entries.isEmpty()) h += tr("<p>Empty folder.</p>");
        QStringList lines;
        const int limit = 60;
        for (int i = 0; i < entries.size() && i < limit; ++i) {
            const auto& e = entries[i];
            if (e.isDir() && !e.isSymLink())
                lines << QStringLiteral("📁 %1/").arg(e.fileName().toHtmlEscaped());
            else
                lines << QStringLiteral("%1 <span style='color:gray'>%2</span>")
                             .arg(e.fileName().toHtmlEscaped(), humanQ(static_cast<quint64>(e.size())));
        }
        if (entries.size() > limit) lines << tr("… and %1 more").arg(entries.size() - limit);
        if (!lines.isEmpty()) h += QStringLiteral("<p>%1</p>").arg(lines.join(QStringLiteral("<br>")));
    }
    m_leftDetails->setHtml(h);
}

void MainWindow::keepLeftovers(bool keep) {
    QList<int> targets = checkedLeftovers();
    if (targets.isEmpty() && m_leftTable->currentRow() >= 0)
        targets << m_leftTable->item(m_leftTable->currentRow(), CheckCol)->data(Qt::UserRole).toInt();
    if (targets.isEmpty()) return;

    for (int i : targets) {
        const QString path = qs(m_leftovers[static_cast<size_t>(i)].path);
        if (keep && !m_kept.contains(path)) m_kept << path;
        if (!keep) m_kept.removeAll(path);
    }
    saveKept();
    populateLeftovers();
    statusBar()->showMessage(keep ? tr("Keeping %1 folders. They won't be listed again.").arg(targets.size())
                                  : tr("No longer keeping %1 folders.").arg(targets.size()),
                             6000);
}

void MainWindow::removeLeftovers() {
    const auto checked = checkedLeftovers();
    if (checked.isEmpty()) {
        QMessageBox::information(this, windowTitle(),
                                 tr("Tick the folders you want to remove first."));
        return;
    }

    QStringList home, system, listing;
    quint64 total = 0;
    for (int i : checked) {
        const auto& l = m_leftovers[static_cast<size_t>(i)];
        const QString path = qs(l.path);
        if (!core::isLeftoverCandidatePath(l.path, l.system)) {
            logLine(tr("Skipped %1: it isn't a folder this tab is allowed to remove.").arg(path));
            continue;
        }
        (l.system ? system : home) << path;
        total += l.size;
        listing << QStringLiteral("%1  (%2)").arg(shortPath(path), humanQ(l.size));
    }
    if (home.isEmpty() && system.isEmpty()) return;

    QString text = tr("Remove %1 folders (about %2)?\n\n%3")
                       .arg(home.size() + system.size())
                       .arg(humanQ(total), listing.join('\n'));
    if (!home.isEmpty()) text += tr("\n\nFolders in your home folder go to the Trash.");
    if (!system.isEmpty())
        text += tr("\n\n⚠ System folders can't go to the Trash. They're deleted "
                   "permanently and need your password.");
    if (!confirm(tr("Remove leftover folders"), text)) return;

    QStringList failed;
    for (const auto& path : home) {
        if (QFile::moveToTrash(path))
            logLine(tr("Moved to Trash: %1").arg(path));
        else
            failed << path;
    }
    if (!failed.isEmpty()) {
        logLine(tr("Couldn't move to Trash: %1").arg(failed.join(QStringLiteral(", "))));
        QMessageBox::warning(this, windowTitle(),
                             tr("These folders couldn't be moved to the Trash:\n\n%1\n\n"
                                "They may be on a drive without a Trash folder. Nothing was "
                                "deleted.").arg(failed.join('\n')));
    }

    if (!system.isEmpty())
        runCommand(QStringList{QStringLiteral("rm"), QStringLiteral("-rf"), QStringLiteral("--")} + system,
                   true, [this](int) { scanLeftovers(); });
    else
        scanLeftovers();
}
