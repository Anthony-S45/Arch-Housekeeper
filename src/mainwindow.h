// mainwindow.h - the main window. Its methods are split across files:
//   mainwindow.cpp   - setup, shared helpers, Applications/Orphans/Cache tabs
//   details.cpp      - the "Why is this installed?" panel
//   configtab.cpp    - the Config files tab (.pacnew / .pacsave)
//   leftoverstab.cpp - the Leftovers tab
#pragma once

#include <QList>
#include <QMainWindow>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QStringList>
#include <QThread>
#include <functional>
#include <vector>

#include "core.h"
#include "inspect.h"

class PackageTable;
class QCheckBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QSpinBox;
class QTableWidget;
class QTabWidget;
class QTextBrowser;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    enum Tab { AppsTab, OrphansTab, ConfigTab, LeftoversTab, CacheTab };

    // ---- mainwindow.cpp ---------------------------------------------------
    QWidget* buildAppsTab();
    QWidget* buildOrphansTab();
    QWidget* buildCacheTab();
    QPushButton* makeButton(const QString& text, std::function<void()> slot,
                            bool action = true);

    // Runs read-only work off the UI thread, then calls done(result) on it.
    template <typename Fn, typename Done>
    void inBackground(Fn fn, Done done) {
        QThread* thread = QThread::create([this, fn, done]() {
            auto result = fn();
            // If the window is gone by then, Qt drops the call.
            QMetaObject::invokeMethod(
                this, [done, result]() { done(result); }, Qt::QueuedConnection);
        });
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        m_threads.removeAll(nullptr);
        m_threads.append(thread);
        thread->start();
    }

    void runCommand(const QStringList& args, bool root,
                    std::function<void(int)> onDone = {});
    void commandFinished(int code, bool root, const std::function<void(int)>& onDone);
    void setBusy(bool busy);
    void logLine(const QString& text);
    void appendRaw(const QString& text);
    bool confirm(const QString& title, const QString& text);
    bool pacmanLocked();
    void updateStatus(PackageTable* table, QLabel* label);
    void setTabCount(Tab tab, const QString& name, int count);

    void refreshAll();
    void scanApps();
    void scanOrphans();
    void scanCache();
    void showCache(const core::CacheInfo& info);

    void exportApps();
    void removePackages(PackageTable* table, const QString& flags, const QString& what);
    void keepOrphans();
    void paccache(const QStringList& options, bool dryRun);
    void emptyCache();
    void enableTimer();
    void clearAur(const QString& name, const QString& path);

    // ---- details.cpp ------------------------------------------------------
    QTextBrowser* makeDetailsBrowser();
    QWidget* withDetails(PackageTable* table, QTextBrowser* details);
    void showDetails(QTextBrowser* browser, const QString& pkg);
    static QString detailsHtml(const core::PackageDetails& d);

    // ---- configtab.cpp ----------------------------------------------------
    QWidget* buildConfigTab();
    void scanConfigs();
    void populateConfigs();
    void showConfigDiff();
    QList<int> selectedConfigs() const;
    void updateConfigButtons();
    void keepCurrentConfigs();
    void useOtherConfigs();

    // ---- leftoverstab.cpp -------------------------------------------------
    QWidget* buildLeftoversTab();
    void scanLeftovers();
    void populateLeftovers();
    void showLeftoverDetails();
    QList<int> checkedLeftovers() const;
    void setLeftoverChecks(bool likelyOnly, bool checked);
    void updateLeftoverStatus();
    void keepLeftovers(bool keep);
    void removeLeftovers();
    void loadKept();
    void saveKept();

    // ---- state ------------------------------------------------------------
    QTabWidget* m_tabs = nullptr;
    QPlainTextEdit* m_log = nullptr;
    bool m_logEndsWithNewline = true;

    PackageTable* m_apps = nullptr;
    QTextBrowser* m_appsDetails = nullptr;
    QLineEdit* m_filter = nullptr;
    QCheckBox* m_showSystem = nullptr;
    QCheckBox* m_removeDeps = nullptr;
    QLabel* m_appsStatus = nullptr;

    PackageTable* m_orphans = nullptr;
    QTextBrowser* m_orphansDetails = nullptr;
    QLabel* m_orphansStatus = nullptr;

    QTableWidget* m_configTable = nullptr;
    QPlainTextEdit* m_diffView = nullptr;
    QLabel* m_configStatus = nullptr;
    QPushButton* m_keepCurrentBtn = nullptr;
    QPushButton* m_useOtherBtn = nullptr;
    std::vector<core::ConfigFile> m_configs;

    QTableWidget* m_leftTable = nullptr;
    QTextBrowser* m_leftDetails = nullptr;
    QPushButton* m_openFolderBtn = nullptr;
    QCheckBox* m_showInUse = nullptr;
    QCheckBox* m_showKept = nullptr;
    QLabel* m_leftStatus = nullptr;
    QPushButton* m_keepBtn = nullptr;
    QPushButton* m_unkeepBtn = nullptr;
    std::vector<core::Leftover> m_leftovers;
    QStringList m_kept;

    QLabel* m_cacheLabel = nullptr;
    QSpinBox* m_keepSpin = nullptr;
    QLabel* m_timerLabel = nullptr;
    QPushButton* m_timerButton = nullptr;
    QGroupBox* m_aurBox = nullptr;
    QWidget* m_aurContent = nullptr;

    QProcess* m_proc = nullptr;
    QList<QPointer<QPushButton>> m_actionButtons;    // disabled while busy
    QList<QPointer<QPushButton>> m_paccacheButtons;  // need pacman-contrib
    QList<QPointer<QThread>> m_threads;
    bool m_hasPaccache = false;
};
