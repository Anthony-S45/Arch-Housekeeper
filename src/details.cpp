// details.cpp - the "Why is this installed?" panel beside the Applications and
// Orphans lists. Dependency names are links you can click to look them up.
#include "mainwindow.h"

#include <QDesktopServices>
#include <QSplitter>
#include <QTextBrowser>
#include <QUrl>
#include <algorithm>
#include <iterator>

#include "packagetable.h"

namespace {

QString esc(const std::string& s) { return QString::fromStdString(s).toHtmlEscaped(); }

QString pkgLinks(const std::vector<std::string>& names, size_t limit = 60) {
    QStringList parts;
    for (size_t i = 0; i < names.size() && i < limit; ++i)
        parts << QStringLiteral("<a href=\"pkg:%1\">%1</a>").arg(esc(names[i]));
    if (names.size() > limit)
        parts << QObject::tr("… and %1 more").arg(names.size() - limit);
    return parts.join(QStringLiteral(", "));
}

QString placeholder() {
    return QObject::tr("<p style='color:gray'>Select a package to see why it's "
                       "installed and what removing it would do.</p>");
}

}  // namespace

QTextBrowser* MainWindow::makeDetailsBrowser() {
    auto* browser = new QTextBrowser;
    browser->setOpenLinks(false);
    browser->setHtml(placeholder());
    connect(browser, &QTextBrowser::anchorClicked, this, [this, browser](const QUrl& url) {
        if (url.scheme() == QLatin1String("pkg"))
            showDetails(browser, url.path());
        else
            QDesktopServices::openUrl(url);
    });
    return browser;
}

QWidget* MainWindow::withDetails(PackageTable* table, QTextBrowser* details) {
    auto* split = new QSplitter(Qt::Horizontal);
    split->addWidget(table);
    split->addWidget(details);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    split->setSizes({760, 420});
    connect(table, &PackageTable::currentPackageChanged, this, [this, details](const QString& pkg) {
        if (pkg.isEmpty()) {
            details->setProperty("pkg", QString());
            details->setHtml(placeholder());
        } else {
            showDetails(details, pkg);
        }
    });
    return split;
}

void MainWindow::showDetails(QTextBrowser* browser, const QString& pkg) {
    browser->setProperty("pkg", pkg);
    browser->setHtml(tr("<p style='color:gray'>Looking up %1…</p>").arg(pkg.toHtmlEscaped()));
    const std::string name = pkg.toStdString();
    inBackground([name] { return core::packageDetails(name); },
                 [browser, pkg](const core::PackageDetails& d) {
                     // Ignore the answer if another package was picked meanwhile.
                     if (browser->property("pkg").toString() == pkg)
                         browser->setHtml(detailsHtml(d));
                 });
}

QString MainWindow::detailsHtml(const core::PackageDetails& d) {
    if (!d.found)
        return tr("<h3>%1</h3><p>This isn't an installed package. It may be provided by "
                  "another package under a different name, or it was just removed.</p>")
            .arg(esc(d.name));

    QString h;
    h += QStringLiteral("<h3>%1 <span style='font-weight:normal;color:gray'>%2</span></h3>")
             .arg(esc(d.name), esc(d.version));
    if (!d.description.empty()) h += QStringLiteral("<p>%1</p>").arg(esc(d.description));
    if (!d.url.empty() && d.url != "None")
        h += QStringLiteral("<p><a href=\"%1\">%1</a></p>").arg(esc(d.url));

    // Why it's here
    const bool byHand = d.installReason.find("Explicitly") != std::string::npos;
    h += QStringLiteral("<h4>%1</h4><p>").arg(tr("Why it's here"));
    h += byHand ? tr("You installed it yourself") : tr("It was pulled in as a dependency of something else");
    if (!d.installDate.empty()) h += tr(", on %1").arg(esc(d.installDate));
    h += QStringLiteral(".</p>");
    if (!d.installedBy.empty())
        h += tr("<p>Installed by the command:<br><code>%1</code></p>").arg(esc(d.installedBy));

    // What needs it
    h += QStringLiteral("<h4>%1</h4><p>").arg(tr("What needs it"));
    if (!d.requiredBy.empty())
        h += tr("Required by: %1").arg(pkgLinks(d.requiredBy));
    else if (byHand)
        h += tr("Nothing else depends on it.");
    else
        h += tr("Nothing depends on it anymore, so it's an orphan.");
    h += QStringLiteral("</p>");
    if (!d.optionalFor.empty())
        h += tr("<p>Optional extra for: %1</p>").arg(pkgLinks(d.optionalFor));

    // What removing it would do
    h += QStringLiteral("<h4>%1</h4><p>").arg(tr("If you remove it"));
    if (!d.requiredBy.empty()) {
        h += tr("pacman won't remove it on its own while the packages above need it. "
                "Removing them first would free it up.");
    } else {
        h += tr("Frees about <b>%1</b>.").arg(QString::fromStdString(core::human(d.freed)));
        std::vector<std::string> others;
        std::copy_if(d.removes.begin(), d.removes.end(), std::back_inserter(others),
                     [&](const std::string& n) { return n != d.name; });
        if (!others.empty())
            h += tr(" These dependencies would go with it, since nothing else needs them: %1")
                     .arg(pkgLinks(others));
    }
    h += QStringLiteral("</p>");

    if (!d.dependsOn.empty())
        h += QStringLiteral("<h4>%1</h4><p>%2</p>")
                 .arg(tr("Depends on (%1)").arg(d.dependsOn.size()), pkgLinks(d.dependsOn));
    return h;
}
