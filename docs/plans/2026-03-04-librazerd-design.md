# librazerd Design

**Goal:** A C23 shared library (`librazerd.so`) that wraps the `razerd` Unix socket protocol, giving non-Python applications full access to Razer device configuration without depending on Python.

**Status:** Design approved, ready for implementation.

---

## Architecture

`librazerd` lives at `librazerd/` in the project tree and builds alongside `librazer` via CMake. It opens two connections on `razerd_open()`:

- `/run/razerd/socket` — unprivileged (all normal operations + notifications)
- `/run/razerd/socket.privileged` — optional, best-effort (firmware flash)

All I/O is synchronous blocking per-call. No internal threads for command operations. Thread safety is achieved via a single `mtx_t` that serialises all socket send+receive pairs.

### Optional Notification Thread (`LIBRAZERD_NOTIFICATIONS`)

When compiled with `-DLIBRAZERD_NOTIFICATIONS=ON` (default):

- A background `thrd_t` reads the unprivileged socket continuously and intercepts `NOTIFY_ID_NEWMOUSE`/`NOTIFY_ID_DELMOUSE` packets that arrive between command replies.
- Events are queued in a `notify_lock`-protected ring buffer (32 slots; oldest dropped if full).
- An `eventfd` (Linux) is signalled whenever the queue becomes non-empty. Callers `poll()`/`select()` this fd from any thread.
- `razerd_read_event()` dequeues the next event non-blockingly.

```
razerd_t internals:
  int       sock_fd           ← unprivileged socket
  int       priv_sock_fd      ← privileged socket (-1 if unavailable)
  mtx_t     lock              ← protects sock_fd send+receive
  [notifications only:]
  thrd_t    notify_thread
  mtx_t     notify_lock       ← protects event queue
  int       notify_eventfd    ← readable when queue non-empty
  razerd_event_t queue[32]
  size_t    queue_head, queue_tail
```

---

## Files

```
librazerd/
  CMakeLists.txt
  librazerd.h       ← public header (installed to include/)
  librazerd.c       ← full implementation
```

---

## Public API

### Lifecycle

```c
razerd_t *razerd_open(void);
void      razerd_close(razerd_t *r);
int       razerd_errno(razerd_t *r);   /* last protocol error code */
```

### Device discovery

```c
int  razerd_rescan(razerd_t *r);
int  razerd_reconfigure(razerd_t *r);
int  razerd_get_mice(razerd_t *r, char ***mice_out, size_t *count_out);
void razerd_free_mice(char **mice, size_t count);
int  razerd_get_mouse_info(razerd_t *r, const char *idstr, uint32_t *flags_out);
```

### LEDs

```c
typedef struct {
    char     name[64];
    uint32_t state;            /* 0=off 1=on */
    uint32_t mode;             /* 0=static 1=spectrum 2=breathing 3=wave 4=reaction */
    uint32_t supported_modes;  /* bitmask of (1<<mode) */
    bool     has_color;
    bool     can_change_color;
    uint8_t  r, g, b;
} razerd_led_t;

int  razerd_get_leds(razerd_t *r, const char *idstr, uint32_t profile_id,
                     razerd_led_t **leds_out, size_t *count_out);
void razerd_free_leds(razerd_led_t *leds);
int  razerd_set_led(razerd_t *r, const char *idstr, uint32_t profile_id,
                    const razerd_led_t *led);
```

### DPI

```c
typedef struct {
    uint32_t id;
    uint32_t res[3];      /* per-dimension resolution, 0 if unused */
    uint32_t dim_mask;    /* which res[] slots are valid */
    uint64_t profile_mask;
    bool     mutable;
} razerd_dpi_mapping_t;

int  razerd_get_dpi_mappings(razerd_t *r, const char *idstr,
                              razerd_dpi_mapping_t **out, size_t *count_out);
void razerd_free_dpi_mappings(razerd_dpi_mapping_t *m);
int  razerd_get_dpi_mapping(razerd_t *r, const char *idstr,
                             uint32_t profile_id, uint32_t axis_id,
                             uint32_t *mapping_id_out);
int  razerd_set_dpi_mapping(razerd_t *r, const char *idstr,
                             uint32_t profile_id, uint32_t mapping_id,
                             uint32_t axis_id);
int  razerd_change_dpi_mapping(razerd_t *r, const char *idstr,
                                uint32_t mapping_id, uint32_t dim_id,
                                uint32_t new_res);
```

### Frequency

```c
int  razerd_get_supported_freqs(razerd_t *r, const char *idstr,
                                 uint32_t **out, size_t *count_out);
void razerd_free_freqs(uint32_t *out);
int  razerd_get_freq(razerd_t *r, const char *idstr,
                     uint32_t profile_id, uint32_t *freq_out);
int  razerd_set_freq(razerd_t *r, const char *idstr,
                     uint32_t profile_id, uint32_t freq);
```

### Firmware

```c
int razerd_get_fw_version(razerd_t *r, const char *idstr,
                           uint8_t *major_out, uint8_t *minor_out);
```

### Profiles

```c
int  razerd_get_profiles(razerd_t *r, const char *idstr,
                          uint32_t **ids_out, size_t *count_out);
void razerd_free_profiles(uint32_t *ids);
int  razerd_get_active_profile(razerd_t *r, const char *idstr, uint32_t *id_out);
int  razerd_set_active_profile(razerd_t *r, const char *idstr, uint32_t id);
int  razerd_get_profile_name(razerd_t *r, const char *idstr, uint32_t profile_id,
                              char **name_out);   /* caller frees with free() */
int  razerd_set_profile_name(razerd_t *r, const char *idstr, uint32_t profile_id,
                              const char *name);  /* UTF-8 input */
```

### Buttons

```c
typedef struct { uint32_t id; char name[64]; } razerd_button_t;
typedef struct { uint32_t id; char name[64]; } razerd_button_func_t;

int  razerd_get_buttons(razerd_t *r, const char *idstr,
                         razerd_button_t **out, size_t *count_out);
void razerd_free_buttons(razerd_button_t *b);
int  razerd_get_button_functions(razerd_t *r, const char *idstr,
                                  razerd_button_func_t **out, size_t *count_out);
void razerd_free_button_functions(razerd_button_func_t *f);
int  razerd_get_button_function(razerd_t *r, const char *idstr,
                                 uint32_t profile_id, uint32_t button_id,
                                 razerd_button_func_t *out);
int  razerd_set_button_function(razerd_t *r, const char *idstr,
                                 uint32_t profile_id, uint32_t button_id,
                                 uint32_t func_id);
```

### Axes

```c
typedef struct { uint32_t id; char name[64]; uint32_t flags; } razerd_axis_t;

int  razerd_get_axes(razerd_t *r, const char *idstr,
                      razerd_axis_t **out, size_t *count_out);
void razerd_free_axes(razerd_axis_t *a);
```

### Privileged operations

```c
int razerd_flash_firmware(razerd_t *r, const char *idstr,
                           const void *image, size_t len);
```

### Notifications (only when `LIBRAZERD_NOTIFICATIONS` defined)

```c
typedef enum {
    RAZERD_EVENT_NEW_MOUSE = 0,
    RAZERD_EVENT_DEL_MOUSE = 1,
} razerd_event_type_t;

typedef struct {
    razerd_event_type_t type;
} razerd_event_t;

int razerd_get_notify_fd(razerd_t *r);               /* poll() this fd */
int razerd_read_event(razerd_t *r, razerd_event_t *ev_out); /* non-blocking */
```

---

## Error Handling

- All functions return `int`: `0` = success, negative = failure.
- Negative values map to standard `errno` constants (`-ENOMEM`, `-ENODEV`, `-ECOMM`, `-EPERM`, `-ETIMEDOUT`).
- `razerd_errno(r)` returns the last protocol-level error code sent by `razerd` itself (e.g. `ERR_NOMOUSE=3`, `ERR_NOLED=4`).
- No global state — all errors are per-handle.

---

## Thread Safety

- A `mtx_t` in `razerd_t` serialises all command send+receive pairs. Multiple threads can call any function concurrently.
- The notification thread holds the mutex only long enough to read a notification packet; command threads are not starved.
- The event queue has its own `notify_lock` separate from the command lock.

---

## Build System

`librazerd/CMakeLists.txt`:

```cmake
option(LIBRAZERD_NOTIFICATIONS "Build with async notification thread support" ON)

add_library(razerd_client SHARED librazerd.c)
target_compile_features(razerd_client PRIVATE c_std_23)
target_compile_options(razerd_client PRIVATE -Wall -Wextra -Wpedantic)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(razerd_client PRIVATE
        -fsanitize=address,undefined)
    target_link_options(razerd_client PRIVATE
        -fsanitize=address,undefined)
endif()

if(LIBRAZERD_NOTIFICATIONS)
    target_compile_definitions(razerd_client PRIVATE LIBRAZERD_NOTIFICATIONS)
    target_link_libraries(razerd_client PRIVATE pthread)
endif()

set_target_properties(razerd_client PROPERTIES
    OUTPUT_NAME "razerd"
    SOVERSION 1)

install(TARGETS razerd_client DESTINATION lib)
install(FILES librazerd.h DESTINATION include)
```

Top-level `CMakeLists.txt` gets `add_subdirectory(librazerd)`.

---

## Sanitizer Compatibility

- No global mutable state
- All heap allocations have matching `razerd_free_*` or are freed on error paths
- No type punning — big-endian conversion via `be32toh`/`htobe32` from `<endian.h>`
- No unbounded string operations — all string writes use `snprintf` or size-checked copies
- No VLAs
