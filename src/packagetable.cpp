// packagetable.cpp
#include "packagetable.h"

#include <QHeaderView>

#include "widgets.h"

namespace {

enum Column { Check, Name, Version, Source, Size, Description, ColumnCount };

}  // namespace

PackageTable::PackageTable(QWidget* parent) : QTableWidget(0, ColumnCount, parent) {
    setHorizontalHeaderLabels({QString(), tr("Package"), tr("Version"), tr("Source"),
                               tr("Size"), tr("Description")});
    verticalHeader()->hide();
    setAlternatingRowColors(true);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setWordWrap(false);

    auto* header = horizontalHeader();
    for (int col = Check; col < Description; ++col)
        header->setSectionResizeMode(col, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(Description, QHeaderView::Stretch);

    connect(this, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (item->column() == Check) emit checksChanged();
    });
    // Selecting a row shows its details; ticking the box marks it for removal.
    connect(this, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        const auto* box = row >= 0 ? item(row, Check) : nullptr;
        emit currentPackageChanged(box ? box->data(Qt::UserRole).toString() : QString());
    });
}

void PackageTable::setPackages(const std::vector<core::Package>& packages) {
    blockSignals(true);
    setSortingEnabled(false);
    setRowCount(0);
    setRowCount(static_cast<int>(packages.size()));
    m_packages = packages;
    m_sizes.clear();

    const Qt::ItemFlags readOnly = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    int row = 0;
    for (const auto& p : packages) {
        const QString name = QString::fromStdString(p.name);
        m_sizes.insert(name, p.size);

        auto* check = new QTableWidgetItem;
        check->setFlags(readOnly | Qt::ItemIsUserCheckable);
        check->setCheckState(Qt::Unchecked);
        check->setData(Qt::UserRole, name);

        auto* size = new NumericItem(QString::fromStdString(core::human(p.size)),
                                     static_cast<qint64>(p.size));
        size->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        QTableWidgetItem* cells[ColumnCount] = {
            check,
            new QTableWidgetItem(name),
            new QTableWidgetItem(QString::fromStdString(p.version)),
            new QTableWidgetItem(p.foreign ? tr("AUR") : tr("Repo")),
            size,
            new QTableWidgetItem(QString::fromStdString(p.description)),
        };
        for (int col = 0; col < ColumnCount; ++col) {
            if (col != Check) cells[col]->setFlags(readOnly);
            setItem(row, col, cells[col]);
        }
        ++row;
    }
    setSortingEnabled(true);
    sortItems(Name, Qt::AscendingOrder);
    blockSignals(false);
    emit checksChanged();
}

QStringList PackageTable::checked() const {
    QStringList names;
    for (int r = 0; r < rowCount(); ++r) {
        const auto* box = item(r, Check);
        if (box && box->checkState() == Qt::Checked)
            names << box->data(Qt::UserRole).toString();
    }
    return names;
}

quint64 PackageTable::checkedSize() const {
    quint64 total = 0;
    for (const auto& name : checked()) total += m_sizes.value(name);
    return total;
}

void PackageTable::setAllChecked(bool checked) {
    blockSignals(true);
    for (int r = 0; r < rowCount(); ++r)
        if (!isRowHidden(r)) item(r, Check)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    blockSignals(false);
    emit checksChanged();
}

void PackageTable::applyFilter(const QString& text) {
    const QString needle = text.trimmed();
    for (int r = 0; r < rowCount(); ++r) {
        const bool match = needle.isEmpty() ||
                           item(r, Name)->text().contains(needle, Qt::CaseInsensitive) ||
                           item(r, Description)->text().contains(needle, Qt::CaseInsensitive);
        setRowHidden(r, !match);
    }
}
