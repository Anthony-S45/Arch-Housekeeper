# Arch Housekeeper

A small Qt 6 app for routine Arch Linux cleanup:

- **Applications**: user-installed apps (no libraries, no base system). Tick and remove, or export the list to your Desktop.
- **Orphans**: dependencies nothing needs anymore. Remove them, or keep them by marking them as explicitly installed.
- **Why is this installed?**: selecting a package on either tab shows who installed it and with what command, what depends on it, and what removing it would free. Package names are links.
- **Config files**: `.pacnew` and `.pacsave` files with a coloured diff against the config in use. Keep yours, or switch to the other version (the current file is backed up as `.bak` first). Account, disk and sudo files are never swapped automatically.
- **Leftovers**: folders apps leave behind in `~/.config`, `~/.local/share`, `~/.local/state`, `~/.cache`, your home dot-folders, and unowned folders in `/etc`, `/opt`, `/var/lib`, `/var/cache` and `/usr/share`. Each folder is marked as a likely leftover (its app was uninstalled), a possible leftover (nothing installed matches its name) or in use. You can look inside, open it in your file manager, keep it (hidden from future scans) or remove it. Home folders go to the Trash.
- **Cache**: pacman cache size, `paccache` cleaning with previews, the weekly `paccache.timer`, and yay/paru build caches.

Root actions run through `pkexec`, so you get your desktop's normal password prompt.

## Install as a package (recommended)

```bash
cd packaging
makepkg -si
```

Remove it later with `sudo pacman -R arch-housekeeper`.

## Build by hand

```bash
sudo pacman -S --needed qt6-base cmake pacman-contrib
cmake -B build
cmake --build build
./build/arch-housekeeper
```

## Layout

- `src/core.*` - pacman queries and parsing, plain C++ with no Qt
- `src/inspect.*` - package details, config files and leftover detection, plain C++
- `src/packagetable.*`, `src/widgets.h` - reusable table pieces
- `src/mainwindow.*` - the window and the Applications, Orphans and Cache tabs
- `src/details.cpp`, `src/configtab.cpp`, `src/leftoverstab.cpp` - the newer panels

## License

GPL-3.0. See [LICENSE](LICENSE).
