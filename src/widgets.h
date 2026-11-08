// widgets.h - small shared UI pieces.
#pragma once

#include <QColor>
#include <QFont>
#include <QSyntaxHighlighter>
#include <QTableWidgetItem>
#include <QTextCharFormat>

// A table cell that shows text but sorts by a number (sizes, dates).
class NumericItem : public QTableWidgetItem {
public:
    NumericItem(const QString& text, qint64 value) : QTableWidgetItem(text) {
        setData(Qt::UserRole, value);
    }
    bool operator<(const QTableWidgetItem& other) const override {
        return data(Qt::UserRole).toLongLong() < other.data(Qt::UserRole).toLongLong();
    }
};

// Colours `diff -u` output: removed lines red, added green, hunks blue.
class DiffHighlighter : public QSyntaxHighlighter {
public:
    using QSyntaxHighlighter::QSyntaxHighlighter;

protected:
    void highlightBlock(const QString& text) override {
        QTextCharFormat fmt;
        if (text.startsWith(QLatin1String("+++")) || text.startsWith(QLatin1String("---"))) {
            fmt.setFontWeight(QFont::Bold);
        } else if (text.startsWith('+')) {
            fmt.setForeground(QColor(0x2e, 0xa0, 0x43));
        } else if (text.startsWith('-')) {
            fmt.setForeground(QColor(0xd9, 0x3f, 0x3f));
        } else if (text.startsWith(QLatin1String("@@"))) {
            fmt.setForeground(QColor(0x4a, 0x86, 0xd8));
        } else {
            return;
        }
        setFormat(0, static_cast<int>(text.size()), fmt);
    }
};
