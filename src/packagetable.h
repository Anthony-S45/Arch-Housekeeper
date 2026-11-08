// packagetable.h - a checkable, sortable, filterable list of packages.
#pragma once

#include <QHash>
#include <QStringList>
#include <QTableWidget>
#include <vector>

#include "core.h"

class PackageTable : public QTableWidget {
    Q_OBJECT

public:
    explicit PackageTable(QWidget* parent = nullptr);

    void setPackages(const std::vector<core::Package>& packages);
    const std::vector<core::Package>& packages() const { return m_packages; }

    QStringList checked() const;
    quint64 checkedSize() const;
    void setAllChecked(bool checked);  // only rows the filter shows
    void applyFilter(const QString& text);

signals:
    void checksChanged();
    void currentPackageChanged(const QString& name);  // empty when none

private:
    std::vector<core::Package> m_packages;
    QHash<QString, quint64> m_sizes;
};
