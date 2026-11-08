// core.cpp - pacman queries and cleanup logic, in plain C++ (no Qt).
#include "core.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace fs = std::filesystem;

namespace core {

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) out.push_back(line);
    return out;
}

std::string homeDir() {
    const char* h = std::getenv("HOME");
    return h ? h : "";
}

std::string run(const std::vector<std::string>& args) {
    if (args.empty()) return {};
    int fds[2];
    // O_CLOEXEC so children spawned at the same time from other threads
    // don't inherit this pipe (which would stop us ever seeing EOF).
    if (pipe2(fds, O_CLOEXEC) != 0) return {};

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null",
                                     O_WRONLY, 0);

    std::vector<char*> argv;
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    std::vector<std::string> env;
    for (char** e = environ; e && *e; ++e)
        if (std::strncmp(*e, "LC_ALL=", 7) != 0) env.emplace_back(*e);
    env.emplace_back("LC_ALL=C");
    std::vector<char*> envp;
    for (auto& e : env) envp.push_back(e.data());
    envp.push_back(nullptr);

    pid_t pid = 0;
    const int rc = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(),
                                envp.data());
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);

    std::string out;
    if (rc == 0) {
        char buf[65536];
        for (;;) {
            const ssize_t n = read(fds[0], buf, sizeof buf);
            if (n > 0) out.append(buf, static_cast<size_t>(n));
            else if (n < 0 && errno == EINTR) continue;
            else break;
        }
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    }
    close(fds[0]);
    return out;
}

std::vector<std::string> splitWords(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

std::uint64_t parseSize(const std::string& text) {
    std::istringstream in(text);
    double value = 0;
    std::string unit;
    if (!(in >> value)) return 0;
    in >> unit;
    static const std::map<std::string, double> units = {
        {"B", 1.0}, {"KiB", 1024.0}, {"MiB", 1024.0 * 1024},
        {"GiB", 1024.0 * 1024 * 1024}, {"TiB", 1024.0 * 1024 * 1024 * 1024}};
    const auto it = units.find(unit);
    return static_cast<std::uint64_t>(value * (it == units.end() ? 1.0 : it->second));
}

std::string human(std::uint64_t bytes) {
    static const char* names[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double n = static_cast<double>(bytes);
    int i = 0;
    while (n >= 1024 && i < 4) {
        n /= 1024;
        ++i;
    }
    char buf[32];
    if (i == 0) std::snprintf(buf, sizeof buf, "%.0f %s", n, names[i]);
    else std::snprintf(buf, sizeof buf, "%.1f %s", n, names[i]);
    return buf;
}

std::map<std::string, Fields> parseQi(const std::string& text) {
    std::map<std::string, Fields> result;
    Fields fields;
    std::string key;
    auto flush = [&] {
        const auto it = fields.find("Name");
        if (it != fields.end()) result[it->second] = fields;
        fields.clear();
        key.clear();
    };
    for (const auto& line : splitLines(text)) {
        if (trim(line).empty()) {
            flush();
            continue;
        }
        const auto colon = line.find(':');
        if (line[0] != ' ' && colon != std::string::npos) {
            key = trim(line.substr(0, colon));
            fields[key] = trim(line.substr(colon + 1));
        } else if (!key.empty()) {
            fields[key] += " " + trim(line);  // wrapped continuation line
        }
    }
    flush();
    return result;
}

bool isKernel(const std::string& pkg) {
    static const std::regex re("^linux(-lts|-zen|-hardened|-rt|-rt-lts)?(-headers)?$");
    return std::regex_match(pkg, re);
}

bool isLibraryName(const std::string& pkg) {
    return startsWith(pkg, "lib") || endsWith(pkg, "-lib") || endsWith(pkg, "-libs");
}

bool isProtected(const std::string& pkg) {
    static const std::set<std::string> names = {
        "pacman", "sudo", "systemd", "glibc", "bash", "coreutils", "util-linux",
        "polkit", "mkinitcpio", "dracut", "grub", "efibootmgr", "systemd-boot",
        "networkmanager", "iwd", "dhcpcd", "sddm", "gdm", "lightdm",
        "plasma-desktop", "gnome-shell", "xorg-server", "mesa", "nvidia",
        "nvidia-open", "nvidia-dkms", "nvidia-utils", "linux-firmware"};
    return names.count(pkg) > 0 || isKernel(pkg);
}

std::set<std::string> dependsOf(const std::string& pkg) {
    std::set<std::string> deps;
    const auto all = parseQi(run({"pacman", "-Qi", pkg}));
    const auto it = all.find(pkg);
    if (it == all.end()) return deps;
    const auto f = it->second.find("Depends On");
    if (f == it->second.end()) return deps;
    for (auto d : splitWords(f->second)) {
        if (d == "None") continue;
        d = d.substr(0, d.find_first_of("<>="));  // drop version constraints
        deps.insert(d);
    }
    return deps;
}

std::set<std::string> basePackages() {
    std::set<std::string> pkgs = {"base", "base-devel", "linux-firmware"};
    for (const auto& p : splitWords(run({"pacman", "-Sgq", "base-devel"})))
        pkgs.insert(p);  // base-devel used to be a group
    for (const auto& p : dependsOf("base")) pkgs.insert(p);
    for (const auto& p : dependsOf("base-devel")) pkgs.insert(p);
    return pkgs;
}

namespace {

std::vector<Package> withInfo(const std::vector<std::string>& names) {
    std::vector<Package> pkgs;
    if (names.empty()) return pkgs;
    const auto foreignList = splitWords(run({"pacman", "-Qqm"}));
    const std::set<std::string> foreign(foreignList.begin(), foreignList.end());

    std::vector<std::string> cmd = {"pacman", "-Qi"};
    cmd.insert(cmd.end(), names.begin(), names.end());
    const auto info = parseQi(run(cmd));

    for (const auto& n : names) {
        Package p;
        p.name = n;
        const auto it = info.find(n);
        if (it != info.end()) {
            const auto& f = it->second;
            if (auto v = f.find("Version"); v != f.end()) p.version = v->second;
            if (auto d = f.find("Description"); d != f.end()) p.description = d->second;
            if (auto s = f.find("Installed Size"); s != f.end()) p.size = parseSize(s->second);
        }
        p.foreign = foreign.count(n) > 0;
        pkgs.push_back(std::move(p));
    }
    return pkgs;
}

}  // namespace

std::vector<Package> findApps(bool includeSystem) {
    const auto explicitPkgs = splitWords(run({"pacman", "-Qqe"}));
    if (explicitPkgs.empty()) return {};

    std::vector<std::string> cmd = {"pacman", "-Ql"};
    cmd.insert(cmd.end(), explicitPkgs.begin(), explicitPkgs.end());
    std::set<std::string> hasBin, hasDesktop;
    const std::string bin = "/usr/bin/", apps = "/usr/share/applications/";
    for (const auto& line : splitLines(run(cmd))) {
        const auto sp = line.find(' ');
        if (sp == std::string::npos) continue;
        const std::string pkg = line.substr(0, sp), path = line.substr(sp + 1);
        if (startsWith(path, bin) && path.size() > bin.size() &&
            path.find('/', bin.size()) == std::string::npos) {
            hasBin.insert(pkg);
        } else if (startsWith(path, apps) && endsWith(path, ".desktop")) {
            hasDesktop.insert(pkg);
        }
    }

    const auto base = includeSystem ? std::set<std::string>{} : basePackages();
    std::vector<std::string> names;
    for (const auto& pkg : explicitPkgs) {
        if (!includeSystem &&
            (base.count(pkg) || isKernel(pkg) || endsWith(pkg, "-ucode")))
            continue;
        const bool launcher = hasDesktop.count(pkg) > 0;
        if (!launcher && !hasBin.count(pkg)) continue;
        if (isLibraryName(pkg) && !launcher) continue;
        names.push_back(pkg);
    }
    return withInfo(names);
}

std::vector<Package> findOrphans() {
    return withInfo(splitWords(run({"pacman", "-Qdtq"})));
}

std::vector<std::string> cacheDirs() {
    auto dirs = splitWords(run({"pacman-conf", "CacheDir"}));
    if (dirs.empty()) dirs.push_back("/var/cache/pacman/pkg/");
    return dirs;
}

std::pair<std::uint64_t, int> dirSize(const std::string& path) {
    std::uint64_t total = 0;
    int count = 0;
    std::error_code ec;
    fs::recursive_directory_iterator it(
        path, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2) || it->is_symlink(e2)) continue;
        const auto size = it->file_size(e2);
        if (e2) continue;
        total += size;
        const auto name = it->path().filename().string();
        if (name.find(".pkg.tar") != std::string::npos && !endsWith(name, ".sig"))
            ++count;
    }
    return {total, count};
}

std::map<std::string, std::string> aurCachePaths() {
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    const std::string cache = (xdg && *xdg) ? xdg : homeDir() + "/.cache";
    return {{"paru", cache + "/paru"}, {"yay", cache + "/yay"}};
}

CacheInfo scanCache() {
    CacheInfo info;
    std::error_code ec;
    for (const auto& d : cacheDirs()) {
        if (!fs::is_directory(d, ec)) continue;
        const auto [size, count] = dirSize(d);
        info.dirs.push_back({d, size, count});
    }
    for (const auto& [name, path] : aurCachePaths()) {
        if (!fs::is_directory(path, ec)) continue;
        info.aur.push_back({name, path, dirSize(path).first});
    }
    info.timerEnabled =
        trim(run({"systemctl", "is-enabled", "paccache.timer"})) == "enabled";
    return info;
}

std::string desktopDir() {
    std::string d = trim(run({"xdg-user-dir", "DESKTOP"}));
    if (d.empty() || d == homeDir()) d = homeDir() + "/Desktop";
    std::error_code ec;
    fs::create_directories(d, ec);
    return d;
}

std::string exportList(const std::vector<Package>& apps) {
    std::vector<Package> repo, aur;
    size_t width = 10;
    for (const auto& p : apps) {
        (p.foreign ? aur : repo).push_back(p);
        width = std::max(width, p.name.size());
    }
    auto byName = [](const Package& a, const Package& b) { return a.name < b.name; };
    std::sort(repo.begin(), repo.end(), byName);
    std::sort(aur.begin(), aur.end(), byName);

    const std::time_t now = std::time(nullptr);
    char day[16], stamp[32], host[256] = "localhost";
    std::strftime(day, sizeof day, "%Y-%m-%d", std::localtime(&now));
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M", std::localtime(&now));
    gethostname(host, sizeof host - 1);

    const std::string path = desktopDir() + "/installed-apps-" + day + ".txt";
    std::ofstream out(path);
    if (!out) return {};
    out << "User-installed applications on " << host << "\n"
        << "Generated " << stamp << "\n\n";
    auto section = [&](const char* title, const std::vector<Package>& group) {
        out << "== " << title << " (" << group.size() << ") ==\n";
        for (const auto& p : group)
            out << p.name << std::string(width + 2 - p.name.size(), ' ')
                << p.version << "\n";
        if (group.empty()) out << "(none)\n";
        out << "\n";
    };
    section("Official repositories", repo);
    section("AUR / foreign", aur);
    return out ? path : std::string{};
}

}  // namespace core
