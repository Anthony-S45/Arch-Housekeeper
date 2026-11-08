// inspect.cpp - "why is this installed?", .pacnew/.pacsave files, and
// leftover folders.
#include "inspect.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <set>

#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace core {

namespace {

// The ARCH_HOUSEKEEPER_* variables only exist so the logic can be tested
// against fake files; normally the real system paths are used.
std::string envOr(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? v : fallback;
}

std::string pacmanLogPath() {
    return envOr("ARCH_HOUSEKEEPER_PACMAN_LOG", "/var/log/pacman.log");
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "Visual Studio Code" / ".vscode-oss" / "code-oss" -> "visualstudiocode" ...
std::string normalize(const std::string& s) {
    std::string out;
    for (unsigned char c : s)
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
    return out;
}

std::vector<std::string> listField(const Fields& f, const char* key, bool stripVersions) {
    std::vector<std::string> out;
    const auto it = f.find(key);
    if (it == f.end()) return out;
    for (auto w : splitWords(it->second)) {
        if (w == "None") continue;
        if (stripVersions) w = w.substr(0, w.find_first_of("<>="));
        out.push_back(w);
    }
    return out;
}

std::string logDate(const std::string& line) {
    return (line.size() > 11 && line[0] == '[') ? line.substr(1, 10) : std::string{};
}

// For "[ALPM] <verb> <name> (version)" lines, returns <name>.
std::string alpmTarget(const std::string& line, const std::string& verb) {
    const std::string tag = "[ALPM] " + verb + " ";
    const auto pos = line.find(tag);
    if (pos == std::string::npos) return {};
    const auto start = pos + tag.size();
    const auto end = line.find(" (", start);
    return end == std::string::npos ? std::string{} : line.substr(start, end - start);
}

std::uint64_t totalInstalledSize(const std::vector<std::string>& names) {
    if (names.empty()) return 0;
    std::vector<std::string> cmd = {"pacman", "-Qi"};
    cmd.insert(cmd.end(), names.begin(), names.end());
    std::uint64_t total = 0;
    for (const auto& [name, f] : parseQi(run(cmd))) {
        const auto it = f.find("Installed Size");
        if (it != f.end()) total += parseSize(it->second);
    }
    return total;
}

}  // namespace

// ---------------------------------------------------------------------------
// Why is this installed?
// ---------------------------------------------------------------------------

PackageDetails packageDetails(const std::string& pkg) {
    PackageDetails d;
    d.name = pkg;
    const auto all = parseQi(run({"pacman", "-Qi", pkg}));
    const auto it = all.find(pkg);
    if (it == all.end()) return d;
    d.found = true;

    const Fields& f = it->second;
    auto get = [&](const char* key) {
        const auto i = f.find(key);
        return i == f.end() ? std::string{} : i->second;
    };
    d.version = get("Version");
    d.description = get("Description");
    d.url = get("URL");
    d.installDate = get("Install Date");
    d.installReason = get("Install Reason");
    d.dependsOn = listField(f, "Depends On", true);
    d.requiredBy = listField(f, "Required By", false);
    d.optionalFor = listField(f, "Optional For", false);

    // Find the pacman command that last installed it.
    std::ifstream log(pacmanLogPath());
    std::string cmd;
    for (std::string line; std::getline(log, line);) {
        const auto runPos = line.find("[PACMAN] Running '");
        if (runPos != std::string::npos) {
            const auto start = runPos + 18;
            const auto end = line.rfind('\'');
            cmd = end > start ? line.substr(start, end - start) : std::string{};
        } else if (alpmTarget(line, "installed") == pkg) {
            d.installedBy = cmd;
            d.installedByDate = logDate(line);
        }
    }
    if (d.installedBy.size() > 240) d.installedBy = d.installedBy.substr(0, 240) + "…";

    // What would `pacman -Rns` take with it? (-p only prints, no root needed.)
    if (d.requiredBy.empty()) {
        d.removes = splitWords(run({"pacman", "-Rnsp", "--print-format", "%n", pkg}));
        if (d.removes.empty()) d.removes.push_back(pkg);
        d.freed = totalInstalledSize(d.removes);
    }
    return d;
}

// ---------------------------------------------------------------------------
// .pacnew / .pacsave
// ---------------------------------------------------------------------------

std::vector<ConfigFile> findConfigFiles() {
    static const std::regex re(R"(^(.*)\.(pacnew|pacsave)(\.[0-9]+)?$)");
    std::set<std::string> paths;

    // pacdiff (pacman-contrib) finds them through pacman's database.
    for (const auto& l : splitLines(run({"pacdiff", "-o"}))) {
        const auto p = trim(l);
        if (!p.empty() && p[0] == '/') paths.insert(p);
    }
    // Also look through /etc directly, in case pacdiff isn't installed.
    std::error_code ec;
    const std::string etc = envOr("ARCH_HOUSEKEEPER_ETC", "/etc");
    fs::recursive_directory_iterator it(etc, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2) && std::regex_match(it->path().filename().string(), re))
            paths.insert(it->path().string());
    }

    std::vector<ConfigFile> out;
    for (const auto& p : paths) {
        std::smatch m;
        if (!std::regex_match(p, m, re)) continue;
        struct stat st {};
        if (::stat(p.c_str(), &st) != 0) continue;
        ConfigFile c;
        c.path = p;
        c.original = m[1].str();
        c.kind = m[2].str();
        c.mtime = st.st_mtime;
        c.originalExists = ::access(c.original.c_str(), F_OK) == 0;
        c.readable = ::access(p.c_str(), R_OK) == 0 &&
                     (!c.originalExists || ::access(c.original.c_str(), R_OK) == 0);
        out.push_back(std::move(c));
    }
    return out;
}

std::string diffFiles(const std::string& current, const std::string& other) {
    return run({"diff", "-u", "-N", "--label", current + "  (current)", "--label", other,
                "--", current, other});
}

// ---------------------------------------------------------------------------
// Leftover folders
// ---------------------------------------------------------------------------

namespace {

thread_local DirStats* g_stats = nullptr;

int statsVisit(const char*, const struct stat* st, int type, struct FTW*) {
    if (type == FTW_DNR || type == FTW_NS) {
        g_stats->partial = true;
        return 0;
    }
    if (type == FTW_F) g_stats->size += static_cast<std::uint64_t>(st->st_size);
    g_stats->newest = std::max<std::int64_t>(g_stats->newest, st->st_mtime);
    return 0;
}

// Name lookup: normalized name -> human-readable explanation.
class NameIndex {
public:
    void add(const std::string& name, const std::string& label) {
        const auto n = normalize(name);
        if (n.size() >= 3) m_map.emplace(n, label);
    }
    const std::string* find(const std::string& normalized) const {
        const auto it = m_map.find(normalized);
        return it == m_map.end() ? nullptr : &it->second;
    }

private:
    std::map<std::string, std::string> m_map;
};

// discord-bin -> discord-bin, discord; libreoffice-fresh -> libreoffice
std::vector<std::string> nameVariants(const std::string& pkg) {
    static const std::vector<std::string> suffixes = {
        "-bin", "-git", "-appimage", "-nightly", "-beta", "-stable", "-dev",
        "-wayland", "-electron", "-full", "-latest", "-canary", "-ptb", "-edition"};
    std::vector<std::string> v = {pkg};
    std::string base = pkg;
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto& s : suffixes) {
            if (base.size() > s.size() && endsWith(base, s)) {
                base.resize(base.size() - s.size());
                v.push_back(base);
                changed = true;
            }
        }
    }
    const auto dash = pkg.find('-');
    if (dash != std::string::npos && dash >= 5) v.push_back(pkg.substr(0, dash));
    return v;
}

void listDir(const std::string& dir, const std::function<void(const fs::directory_entry&)>& fn) {
    std::error_code ec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec))
        fn(*it);
}

void addAppId(NameIndex& idx, const std::string& id, const std::string& label) {
    static const std::set<std::string> stop = {
        "desktop", "gnome", "freedesktop", "github", "gitlab", "flathub",
        "sourceforge", "settings", "client", "application", "portal"};
    idx.add(id, label);
    size_t start = 0;
    while (start <= id.size()) {
        const auto dot = id.find('.', start);
        const auto part = id.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (part.size() >= 4 && !stop.count(lower(part))) idx.add(part, label);
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
}

// Folder names that belong to the desktop or to many programs at once.
const std::set<std::string>& homeIgnore() {
    static const std::set<std::string> names = {
        // ~/.config, ~/.local/share, ~/.cache, ~/.local/state
        "autostart", "systemd", "environment.d", "fontconfig", "fonts", "gtk-2.0",
        "gtk-3.0", "gtk-4.0", "dconf", "pulse", "pipewire", "wireplumber", "menus",
        "mime", "applications", "icons", "themes", "sounds", "trash", "keyrings",
        "gvfs-metadata", "xdg-desktop-portal", "session", "kdedefaults",
        "color-schemes", "plasma", "kxmlgui5", "kded5", "kded6", "knotifications5",
        "knotifications6", "kservices5", "kservices6", "kservicetypes5",
        "kactivitymanagerd", "kscreen", "baloo", "thumbnails", "mesa_shader_cache",
        "mesa_shader_cache_db", "nvidia", "ibus", "fcitx", "fcitx5", "trolltech",
        "qtproject", "flatpak", "containers", "gnome-session", "desktop-directories",
        "dbus-1", "user-places", "arch-housekeeper", "nautilus-python",
        // dot-folders directly in $HOME
        ".ssh", ".gnupg", ".pki", ".local", ".config", ".cache", ".var", ".icons",
        ".fonts", ".themes", ".dbus", ".nv", ".terminfo", ".compose-cache", ".java",
        ".git"};
    return names;
}

bool homeIgnored(const std::string& name) {
    const auto l = lower(name);
    return homeIgnore().count(l) || startsWith(l, "qtshadercache") || startsWith(l, ".trash-");
}

const std::set<std::string>& systemIgnore() {
    static const std::set<std::string> paths = {
        "/var/lib/machines", "/var/lib/portables", "/var/lib/private", "/var/cache/private",
        "/var/lib/systemd", "/var/lib/pacman", "/var/cache/pacman", "/var/lib/dbus",
        "/etc/pacman.d", "/var/cache/ldconfig", "/var/lib/flatpak", "/var/lib/AccountsService",
        "/var/lib/colord", "/var/lib/polkit-1", "/var/lib/upower", "/var/lib/lastlog",
        "/var/lib/os-prober", "/usr/share/info", "/usr/share/applications"};
    return paths;
}

// Folders named after something other than the package that owns them.
const std::map<std::string, std::vector<std::string>>& aliases() {
    static const std::map<std::string, std::vector<std::string>> map = {
        {"mozilla", {"firefox", "firefox-developer-edition", "firefox-nightly", "librewolf",
                     "librewolf-bin", "floorp", "floorp-bin", "zen-browser-bin",
                     "thunderbird", "seamonkey"}},
        {"jetbrains", {"intellij-idea-community-edition", "intellij-idea-ultimate-edition",
                       "pycharm-community-edition", "pycharm-professional", "clion",
                       "webstorm", "goland", "rider", "phpstorm", "datagrip", "rustrover",
                       "jetbrains-toolbox", "android-studio"}},
        {"bravesoftware", {"brave-bin", "brave", "brave-browser"}},
        {"vscode", {"visual-studio-code-bin", "code", "vscodium", "vscodium-bin"}},
        {"vscodeoss", {"code"}},
        {"wine", {"wine", "wine-staging", "wine-ge-custom"}},
    };
    return map;
}

}  // namespace

DirStats dirStats(const std::string& path) {
    DirStats stats;
    g_stats = &stats;
    if (::nftw(path.c_str(), statsVisit, 32, FTW_PHYS) != 0) stats.partial = true;
    g_stats = nullptr;
    return stats;
}

std::map<std::string, std::string> removedPackages() {
    std::map<std::string, std::string> removed;
    std::ifstream log(pacmanLogPath());
    for (std::string line; std::getline(log, line);) {
        if (auto n = alpmTarget(line, "removed"); !n.empty()) removed[n] = logDate(line);
        else if (auto i = alpmTarget(line, "installed"); !i.empty()) removed.erase(i);
    }
    return removed;
}

std::vector<std::string> homeLeftoverRoots() {
    const std::string h = homeDir();
    return {h + "/.config", h + "/.local/share", h + "/.local/state", h + "/.cache"};
}

std::vector<std::string> systemLeftoverRoots() {
    const char* custom = std::getenv("ARCH_HOUSEKEEPER_SYSTEM_ROOTS");
    if (custom && *custom) {
        std::vector<std::string> roots;
        std::string s = custom;
        size_t start = 0;
        while (start < s.size()) {
            auto colon = s.find(':', start);
            if (colon == std::string::npos) colon = s.size();
            if (colon > start) roots.push_back(s.substr(start, colon - start));
            start = colon + 1;
        }
        return roots;
    }
    return {"/etc", "/opt", "/var/lib", "/var/cache", "/usr/share"};
}

bool isLeftoverCandidatePath(const std::string& path, bool system) {
    const fs::path p(path);
    const std::string parent = p.parent_path().string();
    const std::string name = p.filename().string();
    if (name.empty() || name == "." || name == "..") return false;

    struct stat st {};
    if (::lstat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return false;

    if (system) {
        const auto roots = systemLeftoverRoots();
        return std::find(roots.begin(), roots.end(), parent) != roots.end() &&
               !systemIgnore().count(path);
    }
    if (homeIgnored(name)) return false;
    const auto roots = homeLeftoverRoots();
    if (std::find(roots.begin(), roots.end(), parent) != roots.end()) return true;
    return parent == homeDir() && name[0] == '.';
}

std::vector<Leftover> findLeftovers() {
    const std::string home = homeDir();

    // 1. Everything installed: packages, programs, launchers, Flatpak apps.
    NameIndex installed;
    const auto pkgList = splitWords(run({"pacman", "-Qq"}));
    const std::set<std::string> pkgs(pkgList.begin(), pkgList.end());
    for (const auto& pkg : pkgList)
        for (const auto& v : nameVariants(pkg)) installed.add(v, "installed package " + pkg);
    for (const auto& dir : {std::string("/usr/bin"), std::string("/usr/local/bin"), home + "/.local/bin"})
        listDir(dir, [&](const fs::directory_entry& e) {
            const auto n = e.path().filename().string();
            installed.add(n, "installed program " + n);
        });
    for (const auto& dir : {std::string("/usr/share/applications"),
                            home + "/.local/share/applications",
                            std::string("/var/lib/flatpak/exports/share/applications"),
                            home + "/.local/share/flatpak/exports/share/applications"})
        listDir(dir, [&](const fs::directory_entry& e) {
            const auto n = e.path().filename().string();
            if (endsWith(n, ".desktop"))
                addAppId(installed, n.substr(0, n.size() - 8), "app launcher " + n);
        });
    for (const auto& dir : {std::string("/var/lib/flatpak/app"), home + "/.local/share/flatpak/app"})
        listDir(dir, [&](const fs::directory_entry& e) {
            const auto n = e.path().filename().string();
            addAppId(installed, n, "Flatpak app " + n);
        });

    // 2. Packages pacman.log says were removed (and not reinstalled).
    NameIndex removed;
    for (const auto& [name, date] : removedPackages()) {
        if (pkgs.count(name)) continue;
        for (const auto& v : nameVariants(name))
            removed.add(v, name + " was uninstalled on " + date);
    }

    // 3. Folders pacman owns directly under the system locations.
    const auto sysRoots = systemLeftoverRoots();
    const std::set<std::string> sysRootSet(sysRoots.begin(), sysRoots.end());
    std::set<std::string> owned;
    {
        const std::string all = run({"pacman", "-Ql"});
        size_t pos = 0;
        while (pos < all.size()) {
            auto nl = all.find('\n', pos);
            if (nl == std::string::npos) nl = all.size();
            const auto sp = all.find(' ', pos);
            if (sp != std::string::npos && sp < nl && nl > sp + 2 && all[nl - 1] == '/') {
                std::string path = all.substr(sp + 1, nl - sp - 2);  // drop trailing '/'
                const auto slash = path.rfind('/');
                if (slash != std::string::npos && sysRootSet.count(path.substr(0, slash)))
                    owned.insert(std::move(path));
            }
            pos = nl + 1;
        }
    }

    // 4. Collect the candidate folders.
    struct Candidate { std::string path, name; bool system; };
    std::vector<Candidate> candidates;
    auto addDirs = [&](const std::string& root, bool system, bool dotOnly) {
        listDir(root, [&](const fs::directory_entry& e) {
            std::error_code ec;
            if (e.is_symlink(ec) || !e.is_directory(ec)) return;
            const auto name = e.path().filename().string();
            const auto path = e.path().string();
            if (dotOnly && name[0] != '.') return;
            if (system ? (systemIgnore().count(path) || owned.count(path)) : homeIgnored(name))
                return;
            candidates.push_back({path, name, system});
        });
    };
    for (const auto& r : homeLeftoverRoots()) addDirs(r, false, false);
    addDirs(home, false, true);
    for (const auto& r : sysRoots) addDirs(r, true, false);

    // 5. Classify them.
    std::vector<Leftover> out;
    for (const auto& c : candidates) {
        const auto n = normalize(c.name);
        if (n.empty()) continue;
        Leftover l;
        l.path = c.path;
        l.system = c.system;

        const std::string* match = installed.find(n);
        std::string aliasLabel;
        if (!match) {
            const auto a = aliases().find(n);
            if (a != aliases().end())
                for (const auto& p : a->second)
                    if (pkgs.count(p)) {
                        aliasLabel = "installed package " + p;
                        break;
                    }
        }
        if (match || !aliasLabel.empty()) {
            l.verdict = Verdict::InUse;
            l.reason = "Belongs to " + (match ? *match : aliasLabel);
            struct stat st {};
            if (::stat(c.path.c_str(), &st) == 0) l.lastUsed = st.st_mtime;
        } else {
            if (const auto* r = removed.find(n)) {
                l.verdict = Verdict::Likely;
                l.reason = *r;
            } else {
                l.verdict = Verdict::Possibly;
                l.reason = c.system ? "No installed package owns it or matches its name"
                                    : "Nothing installed matches its name";
            }
            const auto s = dirStats(c.path);
            l.size = s.size;
            l.lastUsed = s.newest;
            l.partial = s.partial;
        }
        out.push_back(std::move(l));
    }
    std::sort(out.begin(), out.end(), [](const Leftover& a, const Leftover& b) {
        if (a.verdict != b.verdict) return a.verdict < b.verdict;
        return a.size > b.size;
    });
    return out;
}

}  // namespace core
