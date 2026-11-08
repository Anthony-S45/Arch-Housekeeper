// inspect.h - "why is this installed?", .pacnew/.pacsave files, and leftover
// folders. Plain C++ (no Qt), like core.h.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "core.h"

namespace core {

// ---- Why is this installed? -----------------------------------------------

struct PackageDetails {
    bool found = false;
    std::string name, version, description, url;
    std::string installDate, installReason;
    std::vector<std::string> dependsOn, requiredBy, optionalFor;
    std::string installedBy;      // the pacman command from pacman.log
    std::string installedByDate;  // YYYY-MM-DD
    std::vector<std::string> removes;  // what `pacman -Rns` would take along
    std::uint64_t freed = 0;           // bytes freed by that removal
};

PackageDetails packageDetails(const std::string& pkg);

// ---- .pacnew / .pacsave ---------------------------------------------------

struct ConfigFile {
    std::string path;      // /etc/foo.conf.pacnew
    std::string original;  // /etc/foo.conf
    std::string kind;      // "pacnew" or "pacsave"
    std::int64_t mtime = 0;
    bool originalExists = false;
    bool readable = false;  // can we read both files without root?
};

std::vector<ConfigFile> findConfigFiles();
std::string diffFiles(const std::string& current, const std::string& other);

// ---- Leftover folders -----------------------------------------------------

enum class Verdict { Likely, Possibly, InUse };

struct Leftover {
    std::string path;
    bool system = false;  // outside $HOME, needs root to remove
    Verdict verdict = Verdict::Possibly;
    std::string reason;
    std::uint64_t size = 0;      // not measured for folders in use
    std::int64_t lastUsed = 0;   // newest modification time inside
    bool partial = false;        // some parts couldn't be read
};

struct DirStats {
    std::uint64_t size = 0;
    std::int64_t newest = 0;
    bool partial = false;
};

std::vector<Leftover> findLeftovers();
DirStats dirStats(const std::string& path);
std::map<std::string, std::string> removedPackages();  // name -> date

std::vector<std::string> homeLeftoverRoots();
std::vector<std::string> systemLeftoverRoots();
// Safety check before deleting: is this a top-level folder in one of the
// scanned locations (and not a symlink)?
bool isLeftoverCandidatePath(const std::string& path, bool system);

}  // namespace core
