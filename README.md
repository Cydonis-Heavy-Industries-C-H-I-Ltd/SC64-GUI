# SC64 File Transfer — Qt GUI

A graphical front-end for the [SummerCart64](https://github.com/Polprzewodnikowy/SummerCart64)
N64 flashcart. The SC64 USB protocol is reimplemented directly in C++/Qt
(`QtSerialPort`) — no Rust, no FFI, no child process — and SD-card files are
handled through a vendored **FatFs**, so existing data on the card is preserved.

**(c) [2026 Cydonis Heavy Industries.](https://cydonis.co.uk/about) & contributors.**

## What it does today

- Enumerates SC64 devices by USB VID/PID (FTDI `0x0403:0x6014`).
- Connects, performs the DTR/DSR reset handshake, and verifies the `SC64` id.
- Uploads an N64 ROM into SDRAM (`0x0`) via the `'M'` command in 1 MiB chunks,
  with `.z64/.n64/.v64` byte-order auto-detection and a progress bar.
- **Safely copies files onto the SD card.** Mounts the existing FAT/exFAT volume
  with FatFs and uses `f_write`, so only the FAT, the directory entry, and the
  new file's clusters are touched — everything else on the card is left intact.
- Lists the card's root directory (names, sizes, folders).
- Runs all serial I/O on a worker thread, so the UI never freezes.

The protocol and the FatFs configuration come straight from the upstream
deployer/firmware, so behavior matches the cart's own N64 menu.

## Architecture

| File | Role |
|------|------|
| `src/sc64device.{h,cpp}` | The USB protocol: enumeration, reset, `executeCommand` framing, ROM upload, and the raw `readSectors`/`writeSectors` SD primitives. |
| `src/sc64filesystem.{h,cpp}` | FatFs wrapper: `mount`, `list`, and `copyToCard` (the safe, non-destructive write). |
| `src/fatfs_glue.cpp` | FatFs `disk_read/disk_write/disk_ioctl` routed to the SC64 sector primitives, plus `get_fattime`. |
| `third_party/fatfs/` | Vendored FatFs R0.15a (ChaN, BSD-1) — the same sources the SC64 uses. |
| `src/sc64controller.{h,cpp}` | `Sc64Worker` owns the device + filesystem on a `QThread`; `Sc64Controller` is the QML-facing object. |
| `src/main.cpp`, `qml/Main.qml` | Engine setup and the UI (ROM upload, card listing, safe copy, progress). |

The chain for a safe write is:

```
copyToCard → FatFs f_write → disk_write → Sc64Device::writeSectors → 'M'+'S' commands
```

## Build & run (Debian / Ubuntu)

```bash
sudo apt install build-essential cmake qt6-base-dev qt6-declarative-dev \
  qt6-serialport-dev qml6-module-qtquick-controls qml6-module-qtquick-layouts \
  qml6-module-qtquick-dialogs qt6-wayland

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/sc64gui
```

## Build & install (Arch Linux)

```bash
# Dependencies — all in the official 'extra' repo. On Arch the QtQuick QML
# modules (Controls, Layouts, Dialogs) ship inside qt6-declarative, so there
# are no separate qml-module packages to install.
sudo pacman -S --needed base-devel cmake qt6-base qt6-declarative \
  qt6-serialport qt6-wayland

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run straight from the build tree…
./build/sc64gui

# …or install system-wide (binary, .desktop, icon, AppStream metadata)
sudo cmake --install build --prefix /usr

# Allow your user to reach the cart, then replug it
sudo cp packaging/60-sc64.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

After installing, `sc64gui` is on your `PATH` and shows up in your application
launcher. To uninstall, delete the paths listed in `build/install_manifest.txt`.

> Prefer a proper package? The same `CMakeLists.txt` drops into a `PKGBUILD`
> using the usual `cmake` / `cmake --build` / `DESTDIR="$pkgdir" cmake --install`
> pattern, or you can build the Flatpak from the bundled manifest.

## Linux device access (required)

```bash
sudo cp packaging/60-sc64.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger   # then replug
```

## Flatpak

The manifest adds `--device=all` (USB/serial) and `--filesystem=home:ro` (to read
files you copy). `QtSerialPort` and the QML modules are already in the KDE
runtime; FatFs is compiled from the bundled sources.

## Validating the FatFs layer without hardware

`test/fatfs_ramdisk_test.c` links the vendored FatFs against an in-memory disk
and proves the important property: writing a second file leaves the first one
intact (and long filenames survive). Build and run it with:

```bash
gcc -I third_party/fatfs test/fatfs_ramdisk_test.c build/libfatfs.a -o fftest && ./fftest
```

## Status

Compiled and run (headless) on Qt 6.4. Device enumeration, the threading bridge,
QML load, install/packaging metadata, and the **FatFs mount/write/list logic**
(via the RAM-disk test) are all verified. The SC64 sector transport itself — the
serial commands underneath `writeSectors`/`readSectors` — needs a real cart to
exercise end to end.

## Not done yet/todo:

- **Subdirectory navigation** and delete/rename/mkdir on the card.
- **Booting an uploaded ROM** (CIC/boot-mode config + N64 reset).
- **Saves and 64DD IPL** uploads (same memory-write at different addresses).
- **exFAT note:** enabled in the config, but SDXC cards formatted exFAT carry
  Microsoft patent considerations; FAT32 is the common, friction-free case.
- **Cancellable transfers** and an FTDI raw-USB fast path (libusb).
- **Windows and Android ports... maybe. ;-P
