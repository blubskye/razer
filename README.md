# 🖱️ razercfg

A Linux configuration tool for Razer mice — supporting DPI, LEDs, polling rate, profiles, and button mapping across multiple UI frontends.

---

## ✨ Features

- 🔧 **Per-device configuration** — DPI (with X/Y axis lock), polling rate, LED colour/mode, button mapping, profiles
- 🔁 **Hot-plug** — devices added or removed at runtime are detected automatically
- ⚡ **Fully async UIs** — every hardware operation runs on a background thread; the UI never freezes
- 🖥️ **Three graphical frontends** — Qt 6, GTK 4, or Python/PyQt6
- 💻 **One CLI frontend** — scriptable `razercfg` command-line tool
- 📡 **System daemon** — `razerd` serialises all hardware access; multiple frontends can run simultaneously

---

## 🏗️ Architecture

```
                   ┌──────────────────────────────────────────┐
                   │              Hardware drivers             │
                   │  (hw_deathadder, hw_dav2, hw_basilisk …) │
                   └────────────────────┬─────────────────────┘
                                        │
                                  ┌─────▼──────┐
                                  │  librazer  │  (low-level USB HID)
                                  └─────┬──────┘
                                        │
                                  ┌─────▼──────┐
                                  │   razerd   │  (system daemon, Unix socket)
                                  └──┬──┬──┬───┘
                                     │  │  │
               ┌─────────────────────┘  │  └──────────────────────┐
               │                        │                          │
        ┌──────▼──────┐         ┌───────▼───────┐        ┌────────▼───────┐
        │  librazerd  │         │    pyrazer     │        │   librazerd    │
        │   (C23 API) │         │  (Python API)  │        │   (C23 API)    │
        └──┬──┬──┬────┘         └───────┬────────┘        └────────┬───────┘
           │  │  │                      │                           │
    ┌──────┘  │  └──────────┐           │                           │
    │         │             │           │                           │
┌───▼───┐ ┌──▼───┐ ┌───────▼──┐  ┌────▼─────┐              ┌──────▼──────┐
│  Qt6  │ │ GTK4 │ │ razercfg │  │ qrazercfg│              │ 3rd-party   │
│  GUI  │ │  GUI │ │  (CLI)   │  │ (Python) │              │    apps     │
└───────┘ └──────┘ └──────────┘  └──────────┘              └─────────────┘
```

---

## 🛠️ Languages & Technologies

| Layer | Language / Tech |
|---|---|
| Hardware drivers & daemon (`razerd`) | C99 / C23 |
| Low-level USB library (`librazer`) | C99 |
| Client library (`librazerd`) | C23 |
| Qt 6 frontend | C++17 + Qt 6 |
| GTK 4 frontend | C23 + GTK 4 |
| Python frontend | Python 3 + PyQt6 |
| CLI frontend | C23 |
| Build system | CMake 3.16+ |

---

## 📋 Supported Devices

| Family | Models |
|---|---|
| DeathAdder | Classic, Elite, Essential, V2, V2 Mini, V2 Pro, V2 X, Lite |
| Basilisk | V1–V3 family (8 PIDs) |
| Viper | V1–Ultimate family (5 PIDs) |
| Lancehead | Tournament / Wireless family (5 PIDs) |
| Mamba | Wireless family (3 PIDs) |
| Naga | V2 family (6 PIDs) |
| Abyssus | Logo-LED family (4 PIDs) |
| Orochi | Logo-LED family (3 PIDs) |

---

## 📦 Dependencies

### Runtime

| Dependency | Package (Debian/Ubuntu) | Package (Fedora/RHEL) |
|---|---|---|
| libusb 1.0 | `libusb-1.0-0` | `libusb1` |
| Python 3 | `python3` | `python3` |
| PyQt6 *(Python UI only)* | `python3-pyqt6` | `python3-pyqt6` |
| Qt 6 *(Qt UI only)* | `qt6-base-dev` | `qt6-qtbase-devel` |
| GTK 4 *(GTK UI only)* | `libgtk-4-dev` | `gtk4-devel` |

### Build only

| Dependency | Package (Debian/Ubuntu) | Package (Fedora/RHEL) |
|---|---|---|
| CMake 3.16+ | `cmake` | `cmake` |
| C/C++ compiler | `build-essential` | `gcc gcc-c++` |
| pkg-config | `pkg-config` | `pkgconf` |
| Qt 6 dev tools | `qt6-base-dev qt6-tools-dev-tools` | `qt6-qtbase-devel qt6-qttools-devel` |

---

## 🔨 Building

```sh
# Configure (choose which frontends to build)
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DUI_BACKEND=qt        # or: gtk  python  c

# Build
cmake --build build -j$(nproc)
```

All four backends can be built independently. Omit `-DUI_BACKEND` to build the default (Python). You can also build multiple backends by configuring separate build directories.

### Build all backends

```sh
for backend in qt gtk python c; do
    cmake -B build-$backend -DUI_BACKEND=$backend -DCMAKE_BUILD_TYPE=Release
    cmake --build build-$backend -j$(nproc)
done
```

---

## 🚀 Installing

```sh
# Install daemon, libraries, and selected frontend (run as root)
cmake --install build
```

The default install prefix is `/usr/local`. To change it:

```sh
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr ...
cmake --install build
```

After installing, make sure `librazer.so` and `librazerd.so` are findable by the dynamic linker. On most systems `/usr/local/lib` is already in the search path. If not, add it:

```sh
echo /usr/local/lib | sudo tee /etc/ld.so.conf.d/razercfg.conf
sudo ldconfig
```

---

## ⚙️ Starting the daemon

`razerd` must run as root before any frontend can be used.

### systemd

```sh
sudo systemctl enable --now razerd
```

### SysV init (non-systemd)

```sh
sudo cp razerd.initscript /etc/init.d/razerd
sudo ln -s /etc/init.d/razerd /etc/rc2.d/S99razerd
sudo ln -s /etc/init.d/razerd /etc/rc5.d/S99razerd
sudo ln -s /etc/init.d/razerd /etc/rc0.d/K01razerd
sudo ln -s /etc/init.d/razerd /etc/rc6.d/K01razerd
sudo /etc/init.d/razerd start
```

---

## 🖥️ Using the frontends

Once `razerd` is running, start whichever frontend you prefer:

```sh
# Graphical — Qt 6
qrazercfg            # built from ui/qt/

# Graphical — GTK 4
qrazercfg            # built from ui/gtk/

# Graphical — Python/PyQt6
qrazercfg            # built from ui/python/

# Command-line
razercfg --help
```

All graphical UIs are fully non-blocking — Apply buttons queue hardware operations on background threads so the interface stays responsive during USB command delays.

---

## 🔧 Daemon configuration

An optional config file at `/etc/razer.conf` controls daemon options and initial hardware settings. A documented example is included as `razer.conf` in this repository.

---

## 🖱️ X.Org / Wayland note

If you use X.Org, configure your input device to `/dev/input/mice` (the generic aggregated node) rather than a specific `/dev/input/mouseX`. razerd temporarily unclaims the USB device during configuration events; X will lose and re-acquire the mouse automatically when using the generic node.

```
Section "InputDevice"
    Identifier  "Mouse"
    Driver      "mouse"
    Option      "Device" "/dev/input/mice"
EndSection
```

---

## 🗑️ Uninstalling

```sh
sudo ./uninstall.sh /usr/local   # or your chosen prefix
```

---

## 📄 License

Copyright © 2007–2026 Michael Büsch et al.
See the [`COPYING`](COPYING) file for full license information.
