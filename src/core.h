// core.h - pacman queries and cleanup logic, in plain C++ (no Qt).
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace core {

struct Package {
    std::string name;
    std::string version;
    std::string description;
    std::uint64_t size = 0;  // installed size in bytes
    bool foreign = false;    // true = AUR or locally built
};

struct CacheDir {
    std::string path;
    std::uint64_t size = 0;
    int count = 0;  // package files (not counting .sig files)
};

struct AurCache {
    std::string name;
    std::string path;
    std::uint64_t size = 0;
};

struct CacheInfo {
    std::vector<CacheDir> dirs;
    std::vector<AurCache> aur;
    bool timerEnabled = false;
};

using Fields = std::map<std::string, std::string>;

// Small string helpers shared by core.cpp and inspect.cpp.
inline bool startsWith(const std::string& s, const std::string& prefix) {
    return s.compare(0, prefix.size(), prefix) == 0;
}
inline bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
std::string trim(const std::string& s);
std::vector<std::string> splitLines(const std::string& text);
std::string homeDir();

// Runs a program (looked up on PATH) with LC_ALL=C and returns its stdout.
// The exit code is ignored: pacman exits 1 for "nothing found".
std::string run(const std::vector<std::string>& argv);

std::vector<std::string> splitWords(const std::string& text);
std::uint64_t parseSize(const std::string& text);  // "12.5 MiB" -> bytes
std::string human(std::uint64_t bytes);            // bytes -> "12.5 MiB"
std::map<std::string, Fields> parseQi(const std::string& text);

bool isKernel(const std::string& pkg);
bool isLibraryName(const std::string& pkg);
bool isProtected(const std::string& pkg);  // removal could break the system

std::set<std::string> dependsOf(const std::string& pkg);
std::set<std::string> basePackages();

// Explicitly installed packages that ship a program in /usr/bin or a
// .desktop launcher, minus libraries and (unless includeSystem) base system.
std::vector<Package> findApps(bool includeSystem);
std::vector<Package> findOrphans();

std::vector<std::string> cacheDirs();
std::pair<std::uint64_t, int> dirSize(const std::string& path);
std::map<std::string, std::string> aurCachePaths();
CacheInfo scanCache();

std::string desktopDir();
// Writes the same report as list-apps.sh to the Desktop. Returns the path,
// or an empty string if it couldn't be written.
std::string exportList(const std::vector<Package>& apps);

}  // namespace core
