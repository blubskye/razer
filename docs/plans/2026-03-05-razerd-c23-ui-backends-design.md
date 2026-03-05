# razerd C23 + Multi-Backend UI Design

**Date:** 2026-03-05

**Goal:** Modernise razerd to C23, create C/C++ equivalents of razercfg and
qrazercfg (CLI + Qt6 GUI + GTK4 GUI) backed by librazerd, while keeping the
existing Python/PyQt6 path selectable via a single CMake flag.

---

## Architecture Overview

Five parallel workstreams:

1. **razerd C23 update** — modernise `razerd/razerd.c` from C99 to C23.
2. **razercfg-c** — new C CLI (`ui/c/razercfg.c`) using librazerd.
3. **qrazercfg-qt6** — new C++/Qt6 GUI (`ui/qt/`) using librazerd.
4. **qrazercfg-gtk4** — new C/GTK4 GUI (`ui/gtk/`) using librazerd.
5. **`UI_BACKEND` CMake flag** — single flag selects which frontend is built
   and installed; default stays `python` for backwards compatibility.

---

## Directory Structure

```
razer/
├── razerd/
│   └── razerd.c              # C99 → C23 in-place
├── librazerd/                # unchanged
├── ui/
│   ├── CMakeLists.txt        # UI_BACKEND dispatch
│   ├── python/               # existing files moved here
│   │   ├── razercfg
│   │   ├── qrazercfg
│   │   ├── pyrazer/
│   │   └── CMakeLists.txt
│   ├── c/
│   │   ├── razercfg.c        # C CLI
│   │   └── CMakeLists.txt
│   ├── qt/
│   │   ├── main.cpp
│   │   ├── mainwindow.cpp
│   │   ├── mainwindow.h
│   │   ├── CMakeLists.txt
│   │   └── (per-tab widgets: dpi, led, freq, profiles, buttons)
│   └── gtk/
│       ├── main.c
│       ├── mainwindow.c
│       ├── mainwindow.h
│       ├── CMakeLists.txt
│       └── (per-page widgets: dpi, led, freq, profiles, buttons)
└── docs/
    └── plans/                # tracked in git
```

---

## CMake UI_BACKEND Flag

```cmake
# Top-level ui/CMakeLists.txt
set(UI_BACKEND "python" CACHE STRING "UI backend: python | qt | gtk")
set_property(CACHE UI_BACKEND PROPERTY STRINGS python qt gtk)

if(UI_BACKEND STREQUAL "python")
    add_subdirectory(python)
elseif(UI_BACKEND STREQUAL "qt")
    add_subdirectory(qt)
elseif(UI_BACKEND STREQUAL "gtk")
    add_subdirectory(gtk)
else()
    message(FATAL_ERROR "Unknown UI_BACKEND '${UI_BACKEND}'. Use python, qt, or gtk.")
endif()
```

Usage:
```bash
cmake -DUI_BACKEND=python ..   # default — existing Python+PyQt6
cmake -DUI_BACKEND=qt    ..   # C++/Qt6
cmake -DUI_BACKEND=gtk   ..   # C/GTK4
```

All three backends install to the same names (`razercfg`, `qrazercfg`) so
package maintainers can swap backends without changing downstream scripts.

---

## workstream 1: razerd C23 Modernisation

Changes to `razerd/razerd.c` and `scripts/cmake.global`:

- `-std=c99` → `-std=c2x` in `cmake.global` `GENERIC_COMPILE_FLAGS`
- Replace GNU statement-expression `min`/`max` macros with C23 `typeof`:
  ```c
  #define min(x, y) ({ typeof(x) _x = (x); typeof(y) _y = (y); _x < _y ? _x : _y; })
  // becomes:
  #define min(x, y) ((typeof(x))(x) < (typeof(y))(y) ? (x) : (y))
  ```
  Or use a type-safe inline approach with `_Generic` where needed.
- Remove manual `#define offsetof` — use `<stddef.h>`.
- Add `[[nodiscard]]` to functions returning error codes.
- Replace `_Bool` / `int` boolean returns with `bool` from `<stdbool.h>`
  (already included; audit usages for consistency).
- Add crash signal handler (see Error Handling section).
- Verify clean build under `-fsanitize=address,undefined`.

---

## Workstream 2: razercfg-c (C CLI)

File: `ui/c/razercfg.c`

Matches the Python razercfg flag set:

| Flag | Action |
|------|--------|
| `--mouse <id>` | select device |
| `--listmice` | list connected mice |
| `--setdpi <val>` | set DPI mapping |
| `--getdpi` | print current DPI |
| `--setled <name> <on\|off>` | set LED state |
| `--setfreq <hz>` | set polling frequency |
| `--getfreq` | print current frequency |
| `--setprofile <id>` | activate profile |
| `--getprofile` | print active profile |
| `--setprofname <id> <name>` | set profile name |
| `--getprofname <id>` | get profile name |

Implemented with `getopt_long()`. Links against librazerd.
Built with `-std=c2x -Wall -Wextra -g -rdynamic`.

---

## Workstream 3: qrazercfg-qt6 (C++/Qt6 GUI)

Directory: `ui/qt/`

- **Language:** C++17
- **Dependencies:** Qt6Widgets, Qt6Core; links `librazerd` via `extern "C"`
- **Tabs:** DPI, LED, Frequency, Profiles, Buttons — matching current PyQt6 UI
- **Hot-plug:** `QSocketNotifier` on `razerd_get_notify_fd()` fd; triggers
  device list refresh on `RAZERD_EVENT_NEW_MOUSE` / `RAZERD_EVENT_DEL_MOUSE`
- **Crash handler:** `qInstallMessageHandler()` captures `QtFatalMsg` with
  full stack trace; `SIGSEGV`/`SIGABRT` handler writes trace to stderr and
  `/tmp/qrazercfg-crash-<pid>.log`
- **Build:** CMake `find_package(Qt6 REQUIRED COMPONENTS Widgets)`

---

## Workstream 4: qrazercfg-gtk4 (C/GTK4 GUI)

Directory: `ui/gtk/`

- **Language:** C23
- **Dependencies:** GTK4 (via pkg-config `gtk4`); libadwaita optional
  (`-DUSE_LIBADWAITA=ON`); links librazerd
- **Pages:** `GtkNotebook` with DPI, LED, Frequency, Profiles, Buttons pages
- **Hot-plug:** `g_unix_fd_add()` on `razerd_get_notify_fd()` fd; GLib main
  loop callback refreshes device list
- **Crash handler:** `g_log_set_writer_func()` captures GLib critical/error
  log entries with stack trace appended; `SIGSEGV`/`SIGABRT`/`SIGBUS`
  `sigaction()` handler calls `backtrace()` + `backtrace_symbols_fd()` and
  writes to stderr + `/tmp/qrazercfg-gtk-crash-<pid>.log`
- **Build:** CMake `find_package(PkgConfig)` + `pkg_check_modules(GTK4 gtk4)`

---

## Feature Parity Map

| Feature | pyrazer | librazerd |
|---------|---------|-----------|
| List mice | `Razer.getMice()` | `razerd_get_mice()` |
| Mouse flags | `Razer.getMouseInfo()` | `razerd_get_mouse_info()` |
| DPI mappings | `Razer.getDPIMappings()` | `razerd_get_dpi_mappings()` |
| Change DPI | `Razer.changeDPIMapping()` | `razerd_change_dpi_mapping()` |
| Active DPI | `Razer.getDPIMapping()` | `razerd_get_dpi_mapping()` |
| LEDs | `Razer.getLeds()` | `razerd_get_leds()` |
| Set LED | `Razer.setLed()` | `razerd_set_led()` |
| Frequencies | `Razer.getSupportedFreqs()` | `razerd_get_supported_freqs()` |
| Set frequency | `Razer.setFreq()` | `razerd_set_freq()` |
| Profiles | `Razer.getProfiles()` | `razerd_get_profiles()` |
| Active profile | `Razer.getActiveProfile()` | `razerd_get_active_profile()` |
| Set profile | `Razer.setActiveProfile()` | `razerd_set_active_profile()` |
| Profile name | `Razer.getProfileName()` | `razerd_get_profile_name()` |
| Set prof name | `Razer.setProfileName()` | `razerd_set_profile_name()` |
| Buttons | `Razer.getButtons()` | `razerd_get_buttons()` |
| Button funcs | `Razer.getButtonFunctions()` | `razerd_get_button_functions()` |
| Set button | `Razer.setButtonFunction()` | `razerd_set_button_function()` |
| Hot-plug | `Razer(enableNotifications=True)` | `razerd_get_notify_fd()` + `razerd_read_event()` |

---

## Error Handling & Stack Tracing

### Crash handler (all C/C++ binaries)

Registered at startup via `sigaction()` for `SIGSEGV`, `SIGABRT`, `SIGBUS`:

```c
#include <execinfo.h>
#include <signal.h>

static void crash_handler(int sig)
{
    void *frames[64];
    int n = backtrace(frames, 64);
    /* write to stderr — async-signal-safe */
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    /* also write to /tmp/razercfg-crash-<pid>.log */
    ...
    raise(sig); /* re-raise to get core dump */
}
```

Binaries compiled with `-g -rdynamic` to preserve symbol names.

### Non-fatal errors

A shared `razer_log(level, fmt, ...)` helper (in a small `log.c` / `log.h`
shared by the C CLI and GTK GUI) writes to stderr with a timestamp and log
level prefix. Qt GUI uses `qCritical()` / `qWarning()` routed through the
custom message handler.

### razerd not running

`razerd_open()` returns NULL → all frontends display:
- CLI: `fprintf(stderr, "razerd is not running\n"); exit(1);`
- Qt: `QMessageBox::critical(...)` then `QApplication::exit(1)`
- GTK: `GtkAlertDialog` shown, then `g_application_quit()`

### Hot-plug disconnect

Notification fd becomes readable with `RAZERD_EVENT_DEL_MOUSE` → GUI clears
device from the combo/list and disables controls until a new device appears.

---

## Testing

| Component | Test |
|-----------|------|
| razerd C23 | Clean ASAN+UBSAN build; existing functional behaviour unchanged |
| razercfg-c | Smoke test: `--listmice`, `--getdpi`, `--getfreq` against live razerd |
| Qt GUI | Manual visual verification of all tabs; hot-plug connect/disconnect |
| GTK GUI | Manual visual verification of all tabs; hot-plug connect/disconnect |
| CMake flag | Configure with each `UI_BACKEND` value; confirm correct binary built |
