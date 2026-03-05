# librazerd Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build `librazerd.so`, a thread-safe C23 shared library that wraps the `razerd` Unix socket protocol so non-Python applications can control Razer devices.

**Architecture:** Opaque `razerd_t *` handle owns two Unix stream socket connections (command + optional privileged). A single `mtx_t` serialises all command send+receive. With `LIBRAZERD_NOTIFICATIONS`, a dedicated third socket + background `thrd_t` + `eventfd` delivers async hot-plug events. All functions return 0 on success, negative `errno` on failure.

**Tech Stack:** C23, `<threads.h>` (mtx_t/thrd_t), `<sys/eventfd.h>`, `<endian.h>` (be32toh/htobe32), POSIX Unix sockets, CMake 3.10+.

---

## Protocol Reference (read this before implementing)

Every command is a **512-byte** packet: `[1 byte cmd_id][128 bytes idstr (null-padded)][payload][zero-padding to 512]`.

Replies:
- `REPLY_U32 (0)`: `[1 byte=0][4 bytes BE u32]`
- `REPLY_STR (1)`: `[1 byte=1][1 byte encoding][2 bytes BE length][N bytes]`
  - encoding 0=ASCII, 1=UTF-8, 2=UTF-16-BE (length is code units; bytes = length*2 for UTF-16)

Notifications (arrive on any open socket, no payload after the id byte):
- `NOTIFY_NEWMOUSE (128)`: 1 byte only
- `NOTIFY_DELMOUSE (129)`: 1 byte only

Socket paths: `/run/razerd/socket` (unpriv), `/run/razerd/socket.privileged` (priv).
Interface revision: must equal **6** (received as u32 reply to cmd 0).

**Command IDs:**
```
0  GETREV          1  RESCANMICE      2  GETMICE
3  GETFWVER        4  SUPPFREQS       5  SUPPRESOL
6  SUPPDPIMAPPINGS 7  CHANGEDPIMAPPING 8  GETDPIMAPPING
9  SETDPIMAPPING   10 GETLEDS         11 SETLED
12 GETFREQ         13 SETFREQ         14 GETPROFILES
15 GETACTIVEPROF   16 SETACTIVEPROF   17 SUPPBUTTONS
18 SUPPBUTFUNCS    19 GETBUTFUNC      20 SETBUTFUNC
21 SUPPAXES        22 RECONFIGMICE    23 GETMOUSEINFO
24 GETPROFNAME     25 SETPROFNAME
128 PRIV_FLASHFW   129 PRIV_CLAIM     130 PRIV_RELEASE
```

**Payload layouts** (all multi-byte fields are big-endian):
```
CHANGEDPIMAPPING:  [u32 mapping_id][u32 dimension][u32 new_res]
GETDPIMAPPING:     [u32 profile_id][u32 axis_id]
SETDPIMAPPING:     [u32 profile_id][u32 axis_id][u32 mapping_id]
GETLEDS:           [u32 profile_id]
SETLED:            [u32 profile_id][64 bytes led_name][u8 state][u8 mode][u32 color_0xRRGGBB]
GETFREQ:           [u32 profile_id]
SETFREQ:           [u32 profile_id][u32 new_freq]
SETACTIVEPROF:     [u32 profile_id]
GETPROFNAME:       [u32 profile_id]
SETPROFNAME:       [u32 profile_id][128 bytes UTF-16-BE name (64 chars * 2)]
GETBUTFUNC:        [u32 profile_id][u32 button_id]
SETBUTFUNC:        [u32 profile_id][u32 button_id][u32 function_id]
PRIV_FLASHFW:      [u32 image_size]  (then send image in 128-byte chunks, recv u32 ack each)
```

**Reply layouts for multi-value responses:**
```
GETMICE:           u32 count, then count×string
GETLEDS (per LED): u32 flags, string name, u32 state, u32 mode, u32 supported_modes, u32 color
                   flags: bit0=has_color, bit1=can_change_color
SUPPDPIMAPPINGS:   u32 count, then per mapping:
                     u32 id, u32 dim_mask, 3×u32 res, u32 profile_mask_hi, u32 profile_mask_lo, u32 mutable
SUPPFREQS/SUPPRESOL: u32 count, then count×u32
GETPROFILES:       u32 count, then count×u32
SUPPBUTTONS:       u32 count, then count×(u32 id, string name)
SUPPBUTFUNCS:      u32 count, then count×(u32 id, string name)
GETBUTFUNC:        u32 id, string name
SUPPAXES:          u32 count, then count×(u32 id, string name, u32 flags)
GETMOUSEINFO:      u32 flags  (bit0=result_ok, bit1=global_leds, bit2=profile_leds, ...)
```

---

## Task 1: Directory structure, CMakeLists.txt, and test scaffold

**Files:**
- Create: `librazerd/CMakeLists.txt`
- Create: `librazerd/test_librazerd.c`
- Modify: `CMakeLists.txt` (root)

**Step 1: Create `librazerd/CMakeLists.txt`**

```cmake
include("${razer_SOURCE_DIR}/scripts/cmake.global")

option(LIBRAZERD_NOTIFICATIONS "Build with async notification thread support" ON)

add_library(razerd_client SHARED librazerd.c)

target_compile_options(razerd_client PRIVATE
    -std=c2x
    -Wall -Wextra -Wpedantic -Wformat -Wformat-security
    -Wno-unused-parameter
    -fstack-protector
    -D_GNU_SOURCE
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(razerd_client PRIVATE
        -g
        -fsanitize=address,undefined
        -fsanitize=float-divide-by-zero
        -fsanitize=float-cast-overflow
    )
    target_link_options(razerd_client PRIVATE
        -fsanitize=address,undefined
    )
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

# Test binary (only built, not installed)
add_executable(test_librazerd test_librazerd.c)
target_compile_options(test_librazerd PRIVATE -std=c2x -Wall -g
    -fsanitize=address,undefined)
target_link_options(test_librazerd PRIVATE -fsanitize=address,undefined)
target_link_libraries(test_librazerd PRIVATE razerd_client)
if(LIBRAZERD_NOTIFICATIONS)
    target_compile_definitions(test_librazerd PRIVATE LIBRAZERD_NOTIFICATIONS)
endif()
```

**Step 2: Create placeholder `librazerd/librazerd.h`** (just enough to compile the test)

```c
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct razerd razerd_t;
```

**Step 3: Create placeholder `librazerd/librazerd.c`**

```c
#include "librazerd.h"

struct razerd {
    int placeholder;
};
```

**Step 4: Create `librazerd/test_librazerd.c`**

```c
#include <stdio.h>
#include <stdlib.h>
#include "librazerd.h"

static int tests_run = 0;
static int tests_failed = 0;

#define TEST(name) \
    do { \
        printf("  %-40s ", name); \
        tests_run++; \
    } while(0)

#define PASS() do { printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)
#define ASSERT(cond, msg) \
    do { if (!(cond)) { FAIL(msg); return; } } while(0)

static void test_open_close(void)
{
    TEST("razerd_open/close");
    razerd_t *r = razerd_open();
    ASSERT(r != NULL, "razerd_open returned NULL");
    razerd_close(r);
    PASS();
}

static void test_get_mice(void)
{
    TEST("razerd_get_mice");
    razerd_t *r = razerd_open();
    ASSERT(r != NULL, "razerd_open failed");

    char **mice = NULL;
    size_t count = 0;
    int err = razerd_get_mice(r, &mice, &count);
    ASSERT(err == 0, "razerd_get_mice returned error");
    printf("(%zu mice) ", count);
    razerd_free_mice(mice, count);
    razerd_close(r);
    PASS();
}

int main(void)
{
    printf("librazerd tests (requires running razerd)\n");
    printf("==========================================\n");

    test_open_close();
    test_get_mice();

    printf("\n%d/%d tests passed\n", tests_run - tests_failed, tests_run);
    return tests_failed ? 1 : 0;
}
```

**Step 5: Add `add_subdirectory(librazerd)` to root `CMakeLists.txt`**

In `CMakeLists.txt`, after the `add_subdirectory(librazer)` line (line 24), add:
```cmake
add_subdirectory(librazerd)
```

**Step 6: Verify it compiles**

```bash
cmake --build /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer/build/ 2>&1 | tail -10
```
Expected: `Built target razerd_client` and `Built target test_librazerd`.

**Step 7: Commit**

```bash
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer \
    add librazerd/CMakeLists.txt librazerd/librazerd.h librazerd/librazerd.c \
        librazerd/test_librazerd.c CMakeLists.txt
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer \
    commit -m "Add librazerd build scaffold and test harness"
```

---

## Task 2: Full public header `librazerd.h`

**Files:**
- Modify: `librazerd/librazerd.h`

**Step 1: Replace the placeholder header with the complete public API**

```c
#pragma once
/* librazerd — C23 client library for the razerd daemon socket.
 *
 * All functions return 0 on success, negative errno on failure.
 * Memory returned by razerd_get_*() must be freed with the
 * corresponding razerd_free_*().
 *
 * Thread safety: all functions are safe to call from multiple threads
 * concurrently. A mutex inside razerd_t serialises socket I/O.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Special profile ID meaning "global / no specific profile" */
#define RAZERD_PROFILE_INVALID  0xFFFFFFFFu

/* Mouseinfo flags (razerd_get_mouse_info) */
#define RAZERD_INFO_OK              (1u << 0)
#define RAZERD_INFO_GLOBAL_LEDS     (1u << 1)
#define RAZERD_INFO_PROFILE_LEDS    (1u << 2)
#define RAZERD_INFO_GLOBAL_FREQ     (1u << 3)
#define RAZERD_INFO_PROFILE_FREQ    (1u << 4)
#define RAZERD_INFO_PROFNAME_MUTABLE (1u << 5)
#define RAZERD_INFO_SUGGEST_FWUP    (1u << 6)

/* Axis flags */
#define RAZERD_AXIS_INDEPENDENT_DPIMAPPING (1u << 0)

/* LED mode values */
#define RAZERD_LED_MODE_STATIC      0u
#define RAZERD_LED_MODE_SPECTRUM    1u
#define RAZERD_LED_MODE_BREATHING   2u
#define RAZERD_LED_MODE_WAVE        3u
#define RAZERD_LED_MODE_REACTION    4u

typedef struct razerd razerd_t;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

[[nodiscard]] razerd_t *razerd_open(void);
void                    razerd_close(razerd_t *r);

/* Returns the last protocol-level error code sent by razerd
 * (e.g. 3=ERR_NOMOUSE, 4=ERR_NOLED).  Distinct from the negative
 * errno returned by the function itself. */
int razerd_errno(razerd_t *r);

/* ------------------------------------------------------------------ */
/* Device discovery                                                    */
/* ------------------------------------------------------------------ */

[[nodiscard]] int razerd_rescan(razerd_t *r);
[[nodiscard]] int razerd_reconfigure(razerd_t *r);

/* Returns a heap-allocated array of null-terminated idstrings.
 * Free with razerd_free_mice(). */
[[nodiscard]] int razerd_get_mice(razerd_t *r,
                                   char ***mice_out,
                                   size_t *count_out);
void razerd_free_mice(char **mice, size_t count);

/* flags_out receives RAZERD_INFO_* bitmask */
[[nodiscard]] int razerd_get_mouse_info(razerd_t *r,
                                         const char *idstr,
                                         uint32_t *flags_out);

/* ------------------------------------------------------------------ */
/* LEDs                                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    char     name[64];
    uint32_t state;            /* 0=off, 1=on */
    uint32_t mode;             /* RAZERD_LED_MODE_* */
    uint32_t supported_modes;  /* bitmask: (1u << RAZERD_LED_MODE_X) */
    bool     has_color;
    bool     can_change_color;
    uint8_t  r, g, b;
} razerd_led_t;

/* profile_id: use RAZERD_PROFILE_INVALID for global LEDs */
[[nodiscard]] int razerd_get_leds(razerd_t *r,
                                   const char *idstr,
                                   uint32_t profile_id,
                                   razerd_led_t **leds_out,
                                   size_t *count_out);
void razerd_free_leds(razerd_led_t *leds);

[[nodiscard]] int razerd_set_led(razerd_t *r,
                                  const char *idstr,
                                  uint32_t profile_id,
                                  const razerd_led_t *led);

/* ------------------------------------------------------------------ */
/* DPI                                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t id;
    uint32_t res[3];       /* per-dimension resolution in DPI, 0 if unused */
    uint32_t dim_mask;     /* bitmask: which res[] slots are valid */
    uint64_t profile_mask; /* which profiles use this mapping */
    bool     mutable;
} razerd_dpi_mapping_t;

[[nodiscard]] int razerd_get_dpi_mappings(razerd_t *r,
                                           const char *idstr,
                                           razerd_dpi_mapping_t **out,
                                           size_t *count_out);
void razerd_free_dpi_mappings(razerd_dpi_mapping_t *m);

/* axis_id: use 0xFFFFFFFF for default axis */
[[nodiscard]] int razerd_get_dpi_mapping(razerd_t *r,
                                          const char *idstr,
                                          uint32_t profile_id,
                                          uint32_t axis_id,
                                          uint32_t *mapping_id_out);

[[nodiscard]] int razerd_set_dpi_mapping(razerd_t *r,
                                          const char *idstr,
                                          uint32_t profile_id,
                                          uint32_t mapping_id,
                                          uint32_t axis_id);

[[nodiscard]] int razerd_change_dpi_mapping(razerd_t *r,
                                             const char *idstr,
                                             uint32_t mapping_id,
                                             uint32_t dim_id,
                                             uint32_t new_res);

/* ------------------------------------------------------------------ */
/* Frequency                                                           */
/* ------------------------------------------------------------------ */

[[nodiscard]] int razerd_get_supported_freqs(razerd_t *r,
                                              const char *idstr,
                                              uint32_t **out,
                                              size_t *count_out);
void razerd_free_freqs(uint32_t *out);

[[nodiscard]] int razerd_get_freq(razerd_t *r,
                                   const char *idstr,
                                   uint32_t profile_id,
                                   uint32_t *freq_out);

[[nodiscard]] int razerd_set_freq(razerd_t *r,
                                   const char *idstr,
                                   uint32_t profile_id,
                                   uint32_t freq);

/* ------------------------------------------------------------------ */
/* Firmware                                                            */
/* ------------------------------------------------------------------ */

[[nodiscard]] int razerd_get_fw_version(razerd_t *r,
                                         const char *idstr,
                                         uint8_t *major_out,
                                         uint8_t *minor_out);

/* ------------------------------------------------------------------ */
/* Profiles                                                            */
/* ------------------------------------------------------------------ */

[[nodiscard]] int razerd_get_profiles(razerd_t *r,
                                       const char *idstr,
                                       uint32_t **ids_out,
                                       size_t *count_out);
void razerd_free_profiles(uint32_t *ids);

[[nodiscard]] int razerd_get_active_profile(razerd_t *r,
                                             const char *idstr,
                                             uint32_t *id_out);

[[nodiscard]] int razerd_set_active_profile(razerd_t *r,
                                             const char *idstr,
                                             uint32_t id);

/* name_out is heap-allocated UTF-8; caller frees with free() */
[[nodiscard]] int razerd_get_profile_name(razerd_t *r,
                                           const char *idstr,
                                           uint32_t profile_id,
                                           char **name_out);

/* name is UTF-8; will be converted to UTF-16-BE for the wire */
[[nodiscard]] int razerd_set_profile_name(razerd_t *r,
                                           const char *idstr,
                                           uint32_t profile_id,
                                           const char *name);

/* ------------------------------------------------------------------ */
/* Buttons                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t id;
    char     name[64];
} razerd_button_t;

typedef struct {
    uint32_t id;
    char     name[64];
} razerd_button_func_t;

[[nodiscard]] int razerd_get_buttons(razerd_t *r,
                                      const char *idstr,
                                      razerd_button_t **out,
                                      size_t *count_out);
void razerd_free_buttons(razerd_button_t *b);

[[nodiscard]] int razerd_get_button_functions(razerd_t *r,
                                               const char *idstr,
                                               razerd_button_func_t **out,
                                               size_t *count_out);
void razerd_free_button_functions(razerd_button_func_t *f);

[[nodiscard]] int razerd_get_button_function(razerd_t *r,
                                              const char *idstr,
                                              uint32_t profile_id,
                                              uint32_t button_id,
                                              razerd_button_func_t *out);

[[nodiscard]] int razerd_set_button_function(razerd_t *r,
                                              const char *idstr,
                                              uint32_t profile_id,
                                              uint32_t button_id,
                                              uint32_t func_id);

/* ------------------------------------------------------------------ */
/* Axes                                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t id;
    char     name[64];
    uint32_t flags;  /* RAZERD_AXIS_* */
} razerd_axis_t;

[[nodiscard]] int razerd_get_axes(razerd_t *r,
                                   const char *idstr,
                                   razerd_axis_t **out,
                                   size_t *count_out);
void razerd_free_axes(razerd_axis_t *a);

/* ------------------------------------------------------------------ */
/* Privileged operations (require /run/razerd/socket.privileged)      */
/* ------------------------------------------------------------------ */

/* Sends image in 128-byte chunks; receives u32 ack after each. */
[[nodiscard]] int razerd_flash_firmware(razerd_t *r,
                                         const char *idstr,
                                         const void *image,
                                         size_t len);

/* ------------------------------------------------------------------ */
/* Hot-plug notifications (only available if LIBRAZERD_NOTIFICATIONS) */
/* ------------------------------------------------------------------ */

#ifdef LIBRAZERD_NOTIFICATIONS
typedef enum {
    RAZERD_EVENT_NEW_MOUSE = 0,
    RAZERD_EVENT_DEL_MOUSE = 1,
} razerd_event_type_t;

typedef struct {
    razerd_event_type_t type;
} razerd_event_t;

/* Returns a file descriptor that becomes readable when an event is
 * queued. Safe to pass to poll()/select()/epoll(). */
int razerd_get_notify_fd(razerd_t *r);

/* Non-blocking dequeue. Returns 0 and fills *ev on success,
 * -EAGAIN if queue is empty. */
int razerd_read_event(razerd_t *r, razerd_event_t *ev_out);
#endif /* LIBRAZERD_NOTIFICATIONS */
```

**Step 2: Verify it compiles**

```bash
cmake --build /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer/build/ 2>&1 | tail -5
```
Expected: no errors (the placeholder `.c` doesn't implement anything yet, just compiles).

**Step 3: Commit**

```bash
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer \
    add librazerd/librazerd.h
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer \
    commit -m "Add librazerd public header with complete C23 API"
```

---

## Task 3: Core infrastructure — struct, connect, lifecycle

**Files:**
- Modify: `librazerd/librazerd.c` (replace placeholder with real implementation start)

**Step 1: Write `librazerd.c` up through `razerd_open`/`razerd_close`/`razerd_errno`**

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <endian.h>
#include <threads.h>
#ifdef LIBRAZERD_NOTIFICATIONS
#  include <sys/eventfd.h>
#endif

#include "librazerd.h"

/* ------------------------------------------------------------------ */
/* Internal constants                                                  */
/* ------------------------------------------------------------------ */

#define RAZERD_SOCKPATH         "/run/razerd/socket"
#define RAZERD_PRIV_SOCKPATH    "/run/razerd/socket.privileged"
#define RAZERD_INTERFACE_REV    6u
#define RAZERD_CMD_SIZE         512u
#define RAZERD_IDSTR_MAX        128u
#define RAZERD_LEDNAME_MAX      64u
#define RAZERD_BULK_CHUNK       128u
#define RAZERD_NR_DIMS          3u
#define NOTIFY_QUEUE_SIZE       32u

#define REPLY_U32               0u
#define REPLY_STR               1u
#define NOTIFY_NEWMOUSE         128u
#define NOTIFY_DELMOUSE         129u

#define STR_ENC_ASCII           0u
#define STR_ENC_UTF8            1u
#define STR_ENC_UTF16BE         2u

enum cmd_id {
    CMD_GETREV          = 0,
    CMD_RESCANMICE      = 1,
    CMD_GETMICE         = 2,
    CMD_GETFWVER        = 3,
    CMD_SUPPFREQS       = 4,
    CMD_SUPPRESOL       = 5,
    CMD_SUPPDPIMAPPINGS = 6,
    CMD_CHANGEDPIMAPPING= 7,
    CMD_GETDPIMAPPING   = 8,
    CMD_SETDPIMAPPING   = 9,
    CMD_GETLEDS         = 10,
    CMD_SETLED          = 11,
    CMD_GETFREQ         = 12,
    CMD_SETFREQ         = 13,
    CMD_GETPROFILES     = 14,
    CMD_GETACTIVEPROF   = 15,
    CMD_SETACTIVEPROF   = 16,
    CMD_SUPPBUTTONS     = 17,
    CMD_SUPPBUTFUNCS    = 18,
    CMD_GETBUTFUNC      = 19,
    CMD_SETBUTFUNC      = 20,
    CMD_SUPPAXES        = 21,
    CMD_RECONFIGMICE    = 22,
    CMD_GETMOUSEINFO    = 23,
    CMD_GETPROFNAME     = 24,
    CMD_SETPROFNAME     = 25,
    CMD_PRIV_FLASHFW    = 128,
};

/* ------------------------------------------------------------------ */
/* Internal struct                                                     */
/* ------------------------------------------------------------------ */

#ifdef LIBRAZERD_NOTIFICATIONS
static int notify_thread_fn(void *arg); /* forward decl */
#endif

struct razerd {
    int      cmd_fd;      /* unprivileged command socket */
    int      priv_fd;     /* privileged socket, -1 if unavailable */
    mtx_t    lock;        /* serialises cmd_fd send+receive */
    int      last_err;    /* last protocol-level error from daemon */

#ifdef LIBRAZERD_NOTIFICATIONS
    int             notify_fd;    /* dedicated socket for notification thread */
    int             notify_evfd;  /* eventfd: readable when queue non-empty */
    thrd_t          notify_thr;
    bool            notify_stop;
    mtx_t           notify_lock;
    razerd_event_t  nqueue[NOTIFY_QUEUE_SIZE];
    size_t          nq_head;
    size_t          nq_tail;
#endif
};

/* ------------------------------------------------------------------ */
/* Low-level socket helpers                                            */
/* ------------------------------------------------------------------ */

/* Receive exactly len bytes, retrying on short reads. */
static int recv_all(int fd, void *buf, size_t len)
{
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(fd, (char *)buf + got, len - got, MSG_WAITALL);
        if (n < 0)  return -errno;
        if (n == 0) return -ECONNRESET;
        got += (size_t)n;
    }
    return 0;
}

/* Connect a Unix stream socket to path. Returns fd or -errno. */
static int connect_unix(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -errno;
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        int e = errno;
        close(fd);
        return -e;
    }
    return fd;
}

/* Send GETREV and verify the daemon replies with revision 6. */
static int check_revision(int fd)
{
    /* CMD_GETREV = 0; zero-fill the entire 512-byte packet */
    uint8_t cmd[RAZERD_CMD_SIZE] = {0};
    if (send(fd, cmd, sizeof(cmd), 0) != (ssize_t)sizeof(cmd))
        return -errno;

    uint8_t hdr;
    int err = recv_all(fd, &hdr, 1);
    if (err) return err;
    if (hdr != REPLY_U32) return -EPROTO;

    uint32_t rev_be;
    err = recv_all(fd, &rev_be, 4);
    if (err) return err;
    if (be32toh(rev_be) != RAZERD_INTERFACE_REV)
        return -EPROTO;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

razerd_t *razerd_open(void)
{
    razerd_t *r = calloc(1, sizeof(*r));
    if (!r) return NULL;

    r->priv_fd = -1;
#ifdef LIBRAZERD_NOTIFICATIONS
    r->notify_fd  = -1;
    r->notify_evfd= -1;
#endif

    r->cmd_fd = connect_unix(RAZERD_SOCKPATH);
    if (r->cmd_fd < 0) goto fail_free;

    if (check_revision(r->cmd_fd) < 0) goto fail_cmd;

    /* Best-effort privileged connection */
    r->priv_fd = connect_unix(RAZERD_PRIV_SOCKPATH);
    if (r->priv_fd >= 0)
        (void)check_revision(r->priv_fd); /* ignore; priv may be unavailable */

    if (mtx_init(&r->lock, mtx_plain) != thrd_success)
        goto fail_priv;

#ifdef LIBRAZERD_NOTIFICATIONS
    r->notify_evfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (r->notify_evfd < 0) goto fail_mtx;

    r->notify_fd = connect_unix(RAZERD_SOCKPATH);
    if (r->notify_fd < 0) goto fail_evfd;

    if (check_revision(r->notify_fd) < 0) goto fail_nsock;

    if (mtx_init(&r->notify_lock, mtx_plain) != thrd_success)
        goto fail_nsock;

    r->notify_stop = false;
    if (thrd_create(&r->notify_thr, notify_thread_fn, r) != thrd_success)
        goto fail_nmtx;
#endif

    return r;

    /* Cleanup labels (reverse order of allocation) */
#ifdef LIBRAZERD_NOTIFICATIONS
fail_nmtx:
    mtx_destroy(&r->notify_lock);
fail_nsock:
    close(r->notify_fd);
fail_evfd:
    close(r->notify_evfd);
fail_mtx:
    mtx_destroy(&r->lock);
#endif
fail_priv:
    if (r->priv_fd >= 0) close(r->priv_fd);
fail_cmd:
    close(r->cmd_fd);
fail_free:
    free(r);
    return NULL;
}

void razerd_close(razerd_t *r)
{
    if (!r) return;

#ifdef LIBRAZERD_NOTIFICATIONS
    r->notify_stop = true;
    shutdown(r->notify_fd, SHUT_RDWR); /* unblocks recv() in thread */
    thrd_join(r->notify_thr, NULL);
    close(r->notify_fd);
    close(r->notify_evfd);
    mtx_destroy(&r->notify_lock);
#endif

    mtx_destroy(&r->lock);
    if (r->priv_fd >= 0) close(r->priv_fd);
    close(r->cmd_fd);
    free(r);
}

int razerd_errno(razerd_t *r)
{
    return r->last_err;
}
```

**Step 2: Add stub for notify_thread_fn (needed to compile)**

After the `razerd_close` function, add (inside `#ifdef LIBRAZERD_NOTIFICATIONS`):

```c
#ifdef LIBRAZERD_NOTIFICATIONS
static int notify_thread_fn(void *arg)
{
    razerd_t *r = arg;
    uint8_t pkt;
    while (!r->notify_stop) {
        ssize_t n = recv(r->notify_fd, &pkt, 1, 0);
        if (n <= 0) break;
        /* TODO: implement in Task 11 */
        (void)pkt;
    }
    return 0;
}
#endif
```

**Step 3: Build and verify**

```bash
cmake --build /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer/build/ 2>&1 | tail -8
```
Expected: clean build, no errors.

**Step 4: Run the test (requires razerd running)**

```bash
sudo systemctl start razerd  # or: sudo razerd -f &
cd /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer/build/librazerd
./test_librazerd
```
Expected: `test_open_close` PASS. `test_get_mice` FAIL (not implemented yet).

**Step 5: Commit**

```bash
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer \
    add librazerd/librazerd.c
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer \
    commit -m "Add librazerd core: struct, connect, lifecycle"
```

---

## Task 4: Protocol helpers — send_cmd, recv_u32, recv_str

**Files:**
- Modify: `librazerd/librazerd.c`

These helpers do the actual wire protocol. All command functions in Tasks 5–11 call them. Add them after `razerd_errno`, before the sections that use them.

**Step 1: Add `send_cmd` helper**

```c
/* Build and send a 512-byte command packet while holding r->lock.
 * Caller must hold r->lock before calling. */
static int send_cmd(razerd_t *r, uint8_t cmd_id,
                    const char *idstr,
                    const void *payload, size_t payload_len)
{
    uint8_t buf[RAZERD_CMD_SIZE] = {0};
    buf[0] = cmd_id;

    if (idstr && idstr[0]) {
        size_t idlen = strnlen(idstr, RAZERD_IDSTR_MAX - 1);
        memcpy(buf + 1, idstr, idlen);
        /* null terminator already zeroed by initialisation */
    }

    if (payload && payload_len > 0) {
        /* payload starts after the 1-byte id and 128-byte idstr */
        size_t maxpay = RAZERD_CMD_SIZE - 1u - RAZERD_IDSTR_MAX;
        if (payload_len > maxpay)
            return -EMSGSIZE;
        memcpy(buf + 1 + RAZERD_IDSTR_MAX, payload, payload_len);
    }

    if (send(r->cmd_fd, buf, sizeof(buf), 0) != (ssize_t)sizeof(buf))
        return -errno;
    return 0;
}

/* Same but sends on priv_fd. Returns -EPERM if priv_fd unavailable. */
static int send_priv_cmd(razerd_t *r, uint8_t cmd_id,
                         const char *idstr,
                         const void *payload, size_t payload_len)
{
    if (r->priv_fd < 0) return -EPERM;

    uint8_t buf[RAZERD_CMD_SIZE] = {0};
    buf[0] = cmd_id;
    if (idstr && idstr[0]) {
        size_t idlen = strnlen(idstr, RAZERD_IDSTR_MAX - 1);
        memcpy(buf + 1, idstr, idlen);
    }
    if (payload && payload_len > 0) {
        size_t maxpay = RAZERD_CMD_SIZE - 1u - RAZERD_IDSTR_MAX;
        if (payload_len > maxpay) return -EMSGSIZE;
        memcpy(buf + 1 + RAZERD_IDSTR_MAX, payload, payload_len);
    }
    if (send(r->priv_fd, buf, sizeof(buf), 0) != (ssize_t)sizeof(buf))
        return -errno;
    return 0;
}
```

**Step 2: Add `recv_u32` and `recv_str` helpers**

```c
/* Receive a REPLY_U32 while holding r->lock.
 * If daemon sends an error code instead, stores it in r->last_err
 * and returns -EREMOTEIO. */
static int recv_u32(razerd_t *r, uint32_t *out)
{
    uint8_t hdr;
    int err = recv_all(r->cmd_fd, &hdr, 1);
    if (err) return err;
    if (hdr != REPLY_U32) return -EPROTO;

    uint32_t val_be;
    err = recv_all(r->cmd_fd, &val_be, 4);
    if (err) return err;
    *out = be32toh(val_be);
    return 0;
}

/* Same but reads from priv_fd. */
static int recv_u32_priv(razerd_t *r, uint32_t *out)
{
    uint8_t hdr;
    int err = recv_all(r->priv_fd, &hdr, 1);
    if (err) return err;
    if (hdr != REPLY_U32) return -EPROTO;
    uint32_t val_be;
    err = recv_all(r->priv_fd, &val_be, 4);
    if (err) return err;
    *out = be32toh(val_be);
    return 0;
}

/* Receive a REPLY_STR and return a heap-allocated UTF-8 C string.
 * UTF-16-BE strings are converted to UTF-8 (BMP only).
 * Caller frees with free(). */
static int recv_str(razerd_t *r, char **out)
{
    uint8_t hdr;
    int err = recv_all(r->cmd_fd, &hdr, 1);
    if (err) return err;
    if (hdr != REPLY_STR) return -EPROTO;

    uint8_t enc;
    err = recv_all(r->cmd_fd, &enc, 1);
    if (err) return err;

    uint16_t slen_be;
    err = recv_all(r->cmd_fd, (uint8_t *)&slen_be, 2);
    if (err) return err;
    uint16_t slen = be16toh(slen_be);

    if (enc == STR_ENC_UTF16BE) {
        size_t nbytes = (size_t)slen * 2u;
        uint8_t *raw = malloc(nbytes);
        if (!raw) return -ENOMEM;
        err = recv_all(r->cmd_fd, raw, nbytes);
        if (err) { free(raw); return err; }

        /* Convert BMP UTF-16-BE → UTF-8.
         * Each UTF-16 code unit → up to 3 UTF-8 bytes. */
        char *utf8 = malloc(slen * 3u + 1u);
        if (!utf8) { free(raw); return -ENOMEM; }
        size_t wi = 0;
        for (uint16_t i = 0; i < slen; i++) {
            uint16_t cp = ((uint16_t)raw[i * 2] << 8) | raw[i * 2 + 1];
            if (cp == 0) break;
            if (cp < 0x80u) {
                utf8[wi++] = (char)cp;
            } else if (cp < 0x800u) {
                utf8[wi++] = (char)(0xC0u | (cp >> 6));
                utf8[wi++] = (char)(0x80u | (cp & 0x3Fu));
            } else {
                utf8[wi++] = (char)(0xE0u | (cp >> 12));
                utf8[wi++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
                utf8[wi++] = (char)(0x80u | (cp & 0x3Fu));
            }
        }
        utf8[wi] = '\0';
        free(raw);
        *out = utf8;
        return 0;
    }

    /* ASCII or UTF-8: read directly */
    char *buf = malloc((size_t)slen + 1u);
    if (!buf) return -ENOMEM;
    err = recv_all(r->cmd_fd, buf, slen);
    if (err) { free(buf); return err; }
    buf[slen] = '\0';
    *out = buf;
    return 0;
}
```

**Step 3: Build and verify**

```bash
cmake --build /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer/build/ 2>&1 | tail -5
```
Expected: clean build.

**Step 4: Commit**

```bash
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer \
    add librazerd/librazerd.c
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer \
    commit -m "Add librazerd protocol helpers: send_cmd, recv_u32, recv_str"
```

---

## Task 5: Device discovery

**Files:**
- Modify: `librazerd/librazerd.c`
- Modify: `librazerd/test_librazerd.c`

**Step 1: Implement device discovery functions**

Add after the helpers:

```c
/* ------------------------------------------------------------------ */
/* Device discovery                                                    */
/* ------------------------------------------------------------------ */

int razerd_rescan(razerd_t *r)
{
    mtx_lock(&r->lock);
    int err = send_cmd(r, CMD_RESCANMICE, "", NULL, 0);
    mtx_unlock(&r->lock);
    return err;
}

int razerd_reconfigure(razerd_t *r)
{
    mtx_lock(&r->lock);
    int err = send_cmd(r, CMD_RECONFIGMICE, "", NULL, 0);
    mtx_unlock(&r->lock);
    return err;
}

int razerd_get_mice(razerd_t *r, char ***mice_out, size_t *count_out)
{
    mtx_lock(&r->lock);

    int err = send_cmd(r, CMD_GETMICE, "", NULL, 0);
    if (err) goto unlock;

    uint32_t count;
    err = recv_u32(r, &count);
    if (err) goto unlock;

    char **mice = calloc(count, sizeof(char *));
    if (!mice) { err = -ENOMEM; goto unlock; }

    for (uint32_t i = 0; i < count; i++) {
        err = recv_str(r, &mice[i]);
        if (err) {
            for (uint32_t j = 0; j < i; j++) free(mice[j]);
            free(mice);
            goto unlock;
        }
    }

    *mice_out  = mice;
    *count_out = count;
unlock:
    mtx_unlock(&r->lock);
    return err;
}

void razerd_free_mice(char **mice, size_t count)
{
    if (!mice) return;
    for (size_t i = 0; i < count; i++)
        free(mice[i]);
    free(mice);
}

int razerd_get_mouse_info(razerd_t *r, const char *idstr, uint32_t *flags_out)
{
    mtx_lock(&r->lock);
    int err = send_cmd(r, CMD_GETMOUSEINFO, idstr, NULL, 0);
    if (!err) {
        uint32_t flags;
        err = recv_u32(r, &flags);
        if (!err) {
            if (!(flags & RAZERD_INFO_OK))
                err = -ENODEV;
            else
                *flags_out = flags;
        }
    }
    mtx_unlock(&r->lock);
    return err;
}
```

**Step 2: Expand `test_librazerd.c` — add `test_get_mice` body (already present) and a mouse_info test**

Add after `test_get_mice`:

```c
static void test_mouse_info(void)
{
    TEST("razerd_get_mouse_info");
    razerd_t *r = razerd_open();
    ASSERT(r != NULL, "open failed");

    char **mice = NULL; size_t count = 0;
    int err = razerd_get_mice(r, &mice, &count);
    ASSERT(err == 0, "get_mice failed");

    if (count == 0) {
        printf("(no mice, skipping) ");
        razerd_free_mice(mice, count);
        razerd_close(r);
        PASS();
        return;
    }

    uint32_t flags = 0;
    err = razerd_get_mouse_info(r, mice[0], &flags);
    ASSERT(err == 0, "get_mouse_info failed");
    printf("(flags=0x%x) ", flags);

    razerd_free_mice(mice, count);
    razerd_close(r);
    PASS();
}
```

Add `test_mouse_info()` call in `main`.

**Step 3: Build and run tests**

```bash
cmake --build /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer/build/ 2>&1 | tail -5
cd /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer/build/librazerd && ./test_librazerd
```
Expected: `test_open_close` PASS, `test_get_mice` PASS, `test_mouse_info` PASS.

**Step 4: Commit**

```bash
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer \
    add librazerd/librazerd.c librazerd/test_librazerd.c
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer \
    commit -m "Add librazerd device discovery: rescan, get_mice, get_mouse_info"
```

---

## Task 6: LED operations

**Files:**
- Modify: `librazerd/librazerd.c`
- Modify: `librazerd/test_librazerd.c`

**Step 1: Implement LED functions**

```c
/* ------------------------------------------------------------------ */
/* LEDs                                                                */
/* ------------------------------------------------------------------ */

int razerd_get_leds(razerd_t *r, const char *idstr, uint32_t profile_id,
                    razerd_led_t **leds_out, size_t *count_out)
{
    uint32_t payload_be = htobe32(profile_id);

    mtx_lock(&r->lock);

    int err = send_cmd(r, CMD_GETLEDS, idstr, &payload_be, 4);
    if (err) goto unlock;

    uint32_t count;
    err = recv_u32(r, &count);
    if (err) goto unlock;

    razerd_led_t *leds = calloc(count, sizeof(razerd_led_t));
    if (!leds) { err = -ENOMEM; goto unlock; }

    for (uint32_t i = 0; i < count; i++) {
        uint32_t flags, state, mode, supported_modes, color_u32;
        char *name = NULL;

        if ((err = recv_u32(r, &flags))          != 0) goto fail;
        if ((err = recv_str(r, &name))            != 0) goto fail;
        if ((err = recv_u32(r, &state))           != 0) { free(name); goto fail; }
        if ((err = recv_u32(r, &mode))            != 0) { free(name); goto fail; }
        if ((err = recv_u32(r, &supported_modes)) != 0) { free(name); goto fail; }
        if ((err = recv_u32(r, &color_u32))       != 0) { free(name); goto fail; }

        snprintf(leds[i].name, sizeof(leds[i].name), "%s", name ? name : "");
        free(name);
        leds[i].state           = state;
        leds[i].mode            = mode;
        leds[i].supported_modes = supported_modes;
        leds[i].has_color       = (flags & 0x1u) != 0;
        leds[i].can_change_color= (flags & 0x2u) != 0;
        leds[i].r = (uint8_t)((color_u32 >> 16) & 0xFF);
        leds[i].g = (uint8_t)((color_u32 >>  8) & 0xFF);
        leds[i].b = (uint8_t)((color_u32      ) & 0xFF);
        continue;
fail:
        for (uint32_t j = 0; j < i; j++) { /* name already freed above */ }
        free(leds);
        goto unlock;
    }

    *leds_out  = leds;
    *count_out = count;
unlock:
    mtx_unlock(&r->lock);
    return err;
}

void razerd_free_leds(razerd_led_t *leds)
{
    free(leds);
}

int razerd_set_led(razerd_t *r, const char *idstr, uint32_t profile_id,
                   const razerd_led_t *led)
{
    /* Payload: [u32 profile_id][64 bytes name][u8 state][u8 mode][u32 color] */
    struct __attribute__((packed)) {
        uint32_t profile_id;
        char     led_name[RAZERD_LEDNAME_MAX];
        uint8_t  state;
        uint8_t  mode;
        uint32_t color;
    } payload = {0};

    payload.profile_id = htobe32(profile_id);
    snprintf(payload.led_name, sizeof(payload.led_name), "%s", led->name);
    payload.state = (uint8_t)(led->state ? 1 : 0);
    payload.mode  = (uint8_t)(led->mode & 0xFF);
    uint32_t c = ((uint32_t)led->r << 16) |
                 ((uint32_t)led->g <<  8) |
                  (uint32_t)led->b;
    payload.color = htobe32(c);

    mtx_lock(&r->lock);
    int err = send_cmd(r, CMD_SETLED, idstr, &payload, sizeof(payload));
    if (!err) {
        uint32_t result;
        err = recv_u32(r, &result);
        if (!err && result != 0) {
            r->last_err = (int)result;
            err = -EREMOTEIO;
        }
    }
    mtx_unlock(&r->lock);
    return err;
}
```

Note: `EREMOTEIO` may not exist on all systems. Use `-EIO` instead if it's not defined. Check with `grep -r EREMOTEIO /usr/include` — if missing, substitute `-EIO`.

**Step 2: Add LED test to `test_librazerd.c`**

```c
static void test_leds(void)
{
    TEST("razerd_get_leds");
    razerd_t *r = razerd_open();
    ASSERT(r != NULL, "open failed");

    char **mice = NULL; size_t mc = 0;
    ASSERT(razerd_get_mice(r, &mice, &mc) == 0, "get_mice failed");
    if (mc == 0) {
        printf("(no mice) ");
        razerd_free_mice(mice, mc);
        razerd_close(r);
        PASS();
        return;
    }

    razerd_led_t *leds = NULL; size_t lc = 0;
    int err = razerd_get_leds(r, mice[0], RAZERD_PROFILE_INVALID, &leds, &lc);
    ASSERT(err == 0, "get_leds failed");
    printf("(%zu LEDs) ", lc);
    for (size_t i = 0; i < lc; i++)
        printf("[%s mode=%u] ", leds[i].name, leds[i].mode);

    razerd_free_leds(leds);
    razerd_free_mice(mice, mc);
    razerd_close(r);
    PASS();
}
```

Add `test_leds()` call in `main`.

**Step 3: Build and run**

```bash
cmake --build /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer/build/ 2>&1 | tail -5
cd /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer/build/librazerd && ./test_librazerd
```
Expected: `test_leds` PASS, printing LED names and modes for detected mice.

**Step 4: Commit**

```bash
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer \
    add librazerd/librazerd.c librazerd/test_librazerd.c
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer \
    commit -m "Add librazerd LED operations: get_leds, set_led"
```

---

## Task 7: DPI operations

**Files:**
- Modify: `librazerd/librazerd.c`

**Step 1: Implement DPI functions**

```c
/* ------------------------------------------------------------------ */
/* DPI                                                                 */
/* ------------------------------------------------------------------ */

int razerd_get_dpi_mappings(razerd_t *r, const char *idstr,
                             razerd_dpi_mapping_t **out, size_t *count_out)
{
    mtx_lock(&r->lock);

    int err = send_cmd(r, CMD_SUPPDPIMAPPINGS, idstr, NULL, 0);
    if (err) goto unlock;

    uint32_t count;
    err = recv_u32(r, &count);
    if (err) goto unlock;

    razerd_dpi_mapping_t *m = calloc(count, sizeof(*m));
    if (!m) { err = -ENOMEM; goto unlock; }

    for (uint32_t i = 0; i < count; i++) {
        uint32_t id, dim_mask, pmhi, pmlo, mut;
        if ((err = recv_u32(r, &id))       != 0) goto fail;
        if ((err = recv_u32(r, &dim_mask)) != 0) goto fail;
        m[i].id       = id;
        m[i].dim_mask = dim_mask;
        for (uint32_t d = 0; d < RAZERD_NR_DIMS; d++) {
            uint32_t rv;
            if ((err = recv_u32(r, &rv)) != 0) goto fail;
            m[i].res[d] = (dim_mask & (1u << d)) ? rv : 0u;
        }
        if ((err = recv_u32(r, &pmhi)) != 0) goto fail;
        if ((err = recv_u32(r, &pmlo)) != 0) goto fail;
        m[i].profile_mask = ((uint64_t)pmhi << 32) | pmlo;
        if ((err = recv_u32(r, &mut)) != 0) goto fail;
        m[i].mutable = mut != 0;
        continue;
fail:
        free(m);
        goto unlock;
    }

    *out       = m;
    *count_out = count;
unlock:
    mtx_unlock(&r->lock);
    return err;
}

void razerd_free_dpi_mappings(razerd_dpi_mapping_t *m) { free(m); }

int razerd_get_dpi_mapping(razerd_t *r, const char *idstr,
                            uint32_t profile_id, uint32_t axis_id,
                            uint32_t *mapping_id_out)
{
    uint32_t payload[2] = { htobe32(profile_id), htobe32(axis_id) };
    mtx_lock(&r->lock);
    int err = send_cmd(r, CMD_GETDPIMAPPING, idstr, payload, sizeof(payload));
    if (!err) err = recv_u32(r, mapping_id_out);
    mtx_unlock(&r->lock);
    return err;
}

int razerd_set_dpi_mapping(razerd_t *r, const char *idstr,
                            uint32_t profile_id, uint32_t mapping_id,
                            uint32_t axis_id)
{
    uint32_t payload[3] = {
        htobe32(profile_id), htobe32(axis_id), htobe32(mapping_id)
    };
    mtx_lock(&r->lock);
    int err = send_cmd(r, CMD_SETDPIMAPPING, idstr, payload, sizeof(payload));
    if (!err) {
        uint32_t result;
        err = recv_u32(r, &result);
        if (!err && result != 0) { r->last_err = (int)result; err = -EIO; }
    }
    mtx_unlock(&r->lock);
    return err;
}

int razerd_change_dpi_mapping(razerd_t *r, const char *idstr,
                               uint32_t mapping_id, uint32_t dim_id,
                               uint32_t new_res)
{
    uint32_t payload[3] = {
        htobe32(mapping_id), htobe32(dim_id), htobe32(new_res)
    };
    mtx_lock(&r->lock);
    int err = send_cmd(r, CMD_CHANGEDPIMAPPING, idstr, payload, sizeof(payload));
    if (!err) {
        uint32_t result;
        err = recv_u32(r, &result);
        if (!err && result != 0) { r->last_err = (int)result; err = -EIO; }
    }
    mtx_unlock(&r->lock);
    return err;
}
```

**Step 2: Build**

```bash
cmake --build /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer/build/ 2>&1 | tail -5
```

**Step 3: Commit**

```bash
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer \
    add librazerd/librazerd.c
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer \
    commit -m "Add librazerd DPI operations"
```

---

## Task 8: Frequency + firmware version

**Files:**
- Modify: `librazerd/librazerd.c`

**Step 1: Implement frequency and firmware functions**

```c
/* ------------------------------------------------------------------ */
/* Frequency                                                           */
/* ------------------------------------------------------------------ */

int razerd_get_supported_freqs(razerd_t *r, const char *idstr,
                                uint32_t **out, size_t *count_out)
{
    mtx_lock(&r->lock);
    int err = send_cmd(r, CMD_SUPPFREQS, idstr, NULL, 0);
    if (err) goto unlock;
    uint32_t count;
    err = recv_u32(r, &count);
    if (err) goto unlock;
    uint32_t *freqs = malloc(count * sizeof(uint32_t));
    if (!freqs) { err = -ENOMEM; goto unlock; }
    for (uint32_t i = 0; i < count; i++) {
        err = recv_u32(r, &freqs[i]);
        if (err) { free(freqs); goto unlock; }
    }
    *out = freqs; *count_out = count;
unlock:
    mtx_unlock(&r->lock);
    return err;
}

void razerd_free_freqs(uint32_t *out) { free(out); }

int razerd_get_freq(razerd_t *r, const char *idstr,
                    uint32_t profile_id, uint32_t *freq_out)
{
    uint32_t payload = htobe32(profile_id);
    mtx_lock(&r->lock);
    int err = send_cmd(r, CMD_GETFREQ, idstr, &payload, 4);
    if (!err) err = recv_u32(r, freq_out);
    mtx_unlock(&r->lock);
    return err;
}

int razerd_set_freq(razerd_t *r, const char *idstr,
                    uint32_t profile_id, uint32_t freq)
{
    uint32_t payload[2] = { htobe32(profile_id), htobe32(freq) };
    mtx_lock(&r->lock);
    int err = send_cmd(r, CMD_SETFREQ, idstr, payload, sizeof(payload));
    if (!err) {
        uint32_t result;
        err = recv_u32(r, &result);
        if (!err && result != 0) { r->last_err = (int)result; err = -EIO; }
    }
    mtx_unlock(&r->lock);
    return err;
}

/* ------------------------------------------------------------------ */
/* Firmware version                                                    */
/* ------------------------------------------------------------------ */

int razerd_get_fw_version(razerd_t *r, const char *idstr,
                           uint8_t *major_out, uint8_t *minor_out)
{
    mtx_lock(&r->lock);
    int err = send_cmd(r, CMD_GETFWVER, idstr, NULL, 0);
    if (!err) {
        uint32_t raw;
        err = recv_u32(r, &raw);
        if (!err) {
            *major_out = (uint8_t)((raw >> 8) & 0xFF);
            *minor_out = (uint8_t)(raw & 0xFF);
        }
    }
    mtx_unlock(&r->lock);
    return err;
}
```

**Step 2: Build and commit**

```bash
cmake --build /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer/build/ 2>&1 | tail -5
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer \
    add librazerd/librazerd.c
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer \
    commit -m "Add librazerd frequency and firmware version operations"
```

---

## Task 9: Profiles + profile names

**Files:**
- Modify: `librazerd/librazerd.c`

**Step 1: Implement profile functions**

```c
/* ------------------------------------------------------------------ */
/* Profiles                                                            */
/* ------------------------------------------------------------------ */

int razerd_get_profiles(razerd_t *r, const char *idstr,
                         uint32_t **ids_out, size_t *count_out)
{
    mtx_lock(&r->lock);
    int err = send_cmd(r, CMD_GETPROFILES, idstr, NULL, 0);
    if (err) goto unlock;
    uint32_t count;
    err = recv_u32(r, &count);
    if (err) goto unlock;
    uint32_t *ids = malloc(count * sizeof(uint32_t));
    if (!ids) { err = -ENOMEM; goto unlock; }
    for (uint32_t i = 0; i < count; i++) {
        err = recv_u32(r, &ids[i]);
        if (err) { free(ids); goto unlock; }
    }
    *ids_out = ids; *count_out = count;
unlock:
    mtx_unlock(&r->lock);
    return err;
}

void razerd_free_profiles(uint32_t *ids) { free(ids); }

int razerd_get_active_profile(razerd_t *r, const char *idstr, uint32_t *id_out)
{
    mtx_lock(&r->lock);
    int err = send_cmd(r, CMD_GETACTIVEPROF, idstr, NULL, 0);
    if (!err) err = recv_u32(r, id_out);
    mtx_unlock(&r->lock);
    return err;
}

int razerd_set_active_profile(razerd_t *r, const char *idstr, uint32_t id)
{
    uint32_t payload = htobe32(id);
    mtx_lock(&r->lock);
    int err = send_cmd(r, CMD_SETACTIVEPROF, idstr, &payload, 4);
    if (!err) {
        uint32_t result;
        err = recv_u32(r, &result);
        if (!err && result != 0) { r->last_err = (int)result; err = -EIO; }
    }
    mtx_unlock(&r->lock);
    return err;
}

int razerd_get_profile_name(razerd_t *r, const char *idstr,
                             uint32_t profile_id, char **name_out)
{
    uint32_t payload = htobe32(profile_id);
    mtx_lock(&r->lock);
    int err = send_cmd(r, CMD_GETPROFNAME, idstr, &payload, 4);
    if (!err) err = recv_str(r, name_out);
    mtx_unlock(&r->lock);
    return err;
}

int razerd_set_profile_name(razerd_t *r, const char *idstr,
                             uint32_t profile_id, const char *name)
{
    /* Payload: [u32 profile_id][128 bytes UTF-16-BE name (64 code units)] */
    struct __attribute__((packed)) {
        uint32_t profile_id;
        uint8_t  name_utf16be[64 * 2];
    } payload = {0};

    payload.profile_id = htobe32(profile_id);

    /* Convert UTF-8 input to UTF-16-BE (BMP only), max 64 code units */
    const uint8_t *src = (const uint8_t *)name;
    size_t wi = 0;
    while (*src && wi < 64u) {
        uint32_t cp;
        if ((*src & 0x80u) == 0) {
            cp = *src++;
        } else if ((*src & 0xE0u) == 0xC0u) {
            cp = ((uint32_t)(*src++ & 0x1Fu) << 6) | (*src++ & 0x3Fu);
        } else if ((*src & 0xF0u) == 0xE0u) {
            cp  = ((uint32_t)(*src++ & 0x0Fu) << 12);
            cp |= ((uint32_t)(*src++ & 0x3Fu) <<  6);
            cp |= (*src++ & 0x3Fu);
        } else {
            src++; /* skip invalid/non-BMP */
            continue;
        }
        payload.name_utf16be[wi * 2]     = (uint8_t)(cp >> 8);
        payload.name_utf16be[wi * 2 + 1] = (uint8_t)(cp & 0xFF);
        wi++;
    }

    mtx_lock(&r->lock);
    int err = send_cmd(r, CMD_SETPROFNAME, idstr, &payload, sizeof(payload));
    if (!err) {
        uint32_t result;
        err = recv_u32(r, &result);
        if (!err && result != 0) { r->last_err = (int)result; err = -EIO; }
    }
    mtx_unlock(&r->lock);
    return err;
}
```

**Step 2: Build and commit**

```bash
cmake --build /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer/build/ 2>&1 | tail -5
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer \
    add librazerd/librazerd.c
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer \
    commit -m "Add librazerd profile operations"
```

---

## Task 10: Buttons + axes

**Files:**
- Modify: `librazerd/librazerd.c`

**Step 1: Implement button and axis functions**

```c
/* ------------------------------------------------------------------ */
/* Buttons                                                             */
/* ------------------------------------------------------------------ */

int razerd_get_buttons(razerd_t *r, const char *idstr,
                        razerd_button_t **out, size_t *count_out)
{
    mtx_lock(&r->lock);
    int err = send_cmd(r, CMD_SUPPBUTTONS, idstr, NULL, 0);
    if (err) goto unlock;
    uint32_t count;
    err = recv_u32(r, &count);
    if (err) goto unlock;
    razerd_button_t *b = calloc(count, sizeof(*b));
    if (!b) { err = -ENOMEM; goto unlock; }
    for (uint32_t i = 0; i < count; i++) {
        char *name = NULL;
        err = recv_u32(r, &b[i].id);
        if (err) { free(b); goto unlock; }
        err = recv_str(r, &name);
        if (err) { free(b); goto unlock; }
        snprintf(b[i].name, sizeof(b[i].name), "%s", name ? name : "");
        free(name);
    }
    *out = b; *count_out = count;
unlock:
    mtx_unlock(&r->lock);
    return err;
}

void razerd_free_buttons(razerd_button_t *b) { free(b); }

int razerd_get_button_functions(razerd_t *r, const char *idstr,
                                 razerd_button_func_t **out, size_t *count_out)
{
    mtx_lock(&r->lock);
    int err = send_cmd(r, CMD_SUPPBUTFUNCS, idstr, NULL, 0);
    if (err) goto unlock;
    uint32_t count;
    err = recv_u32(r, &count);
    if (err) goto unlock;
    razerd_button_func_t *f = calloc(count, sizeof(*f));
    if (!f) { err = -ENOMEM; goto unlock; }
    for (uint32_t i = 0; i < count; i++) {
        char *name = NULL;
        err = recv_u32(r, &f[i].id);
        if (err) { free(f); goto unlock; }
        err = recv_str(r, &name);
        if (err) { free(f); goto unlock; }
        snprintf(f[i].name, sizeof(f[i].name), "%s", name ? name : "");
        free(name);
    }
    *out = f; *count_out = count;
unlock:
    mtx_unlock(&r->lock);
    return err;
}

void razerd_free_button_functions(razerd_button_func_t *f) { free(f); }

int razerd_get_button_function(razerd_t *r, const char *idstr,
                                uint32_t profile_id, uint32_t button_id,
                                razerd_button_func_t *out)
{
    uint32_t payload[2] = { htobe32(profile_id), htobe32(button_id) };
    mtx_lock(&r->lock);
    int err = send_cmd(r, CMD_GETBUTFUNC, idstr, payload, sizeof(payload));
    if (!err) {
        char *name = NULL;
        err = recv_u32(r, &out->id);
        if (!err) err = recv_str(r, &name);
        if (!err) {
            snprintf(out->name, sizeof(out->name), "%s", name ? name : "");
            free(name);
        }
    }
    mtx_unlock(&r->lock);
    return err;
}

int razerd_set_button_function(razerd_t *r, const char *idstr,
                                uint32_t profile_id, uint32_t button_id,
                                uint32_t func_id)
{
    uint32_t payload[3] = {
        htobe32(profile_id), htobe32(button_id), htobe32(func_id)
    };
    mtx_lock(&r->lock);
    int err = send_cmd(r, CMD_SETBUTFUNC, idstr, payload, sizeof(payload));
    if (!err) {
        uint32_t result;
        err = recv_u32(r, &result);
        if (!err && result != 0) { r->last_err = (int)result; err = -EIO; }
    }
    mtx_unlock(&r->lock);
    return err;
}

/* ------------------------------------------------------------------ */
/* Axes                                                                */
/* ------------------------------------------------------------------ */

int razerd_get_axes(razerd_t *r, const char *idstr,
                     razerd_axis_t **out, size_t *count_out)
{
    mtx_lock(&r->lock);
    int err = send_cmd(r, CMD_SUPPAXES, idstr, NULL, 0);
    if (err) goto unlock;
    uint32_t count;
    err = recv_u32(r, &count);
    if (err) goto unlock;
    razerd_axis_t *a = calloc(count, sizeof(*a));
    if (!a) { err = -ENOMEM; goto unlock; }
    for (uint32_t i = 0; i < count; i++) {
        char *name = NULL;
        err = recv_u32(r, &a[i].id);
        if (err) { free(a); goto unlock; }
        err = recv_str(r, &name);
        if (err) { free(a); goto unlock; }
        snprintf(a[i].name, sizeof(a[i].name), "%s", name ? name : "");
        free(name);
        err = recv_u32(r, &a[i].flags);
        if (err) { free(a); goto unlock; }
    }
    *out = a; *count_out = count;
unlock:
    mtx_unlock(&r->lock);
    return err;
}

void razerd_free_axes(razerd_axis_t *a) { free(a); }
```

**Step 2: Build and commit**

```bash
cmake --build /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer/build/ 2>&1 | tail -5
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer \
    add librazerd/librazerd.c
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer \
    commit -m "Add librazerd button and axis operations"
```

---

## Task 11: Privileged operations (flash firmware)

**Files:**
- Modify: `librazerd/librazerd.c`

**Step 1: Implement `razerd_flash_firmware`**

```c
/* ------------------------------------------------------------------ */
/* Privileged operations                                               */
/* ------------------------------------------------------------------ */

int razerd_flash_firmware(razerd_t *r, const char *idstr,
                           const void *image, size_t len)
{
    if (r->priv_fd < 0) return -EPERM;

    uint32_t payload = htobe32((uint32_t)len);
    /* Note: firmware flash uses the privileged socket directly.
     * No mtx needed here since priv_fd is only used in this function
     * and razerd_open. For production use, add a priv_lock. */
    int err = send_priv_cmd(r, CMD_PRIV_FLASHFW, idstr, &payload, 4);
    if (err) return err;

    /* Send image in RAZERD_BULK_CHUNK-byte chunks, recv u32 ack each */
    const uint8_t *src = (const uint8_t *)image;
    size_t sent = 0;
    while (sent < len) {
        size_t chunk = len - sent;
        if (chunk > RAZERD_BULK_CHUNK) chunk = RAZERD_BULK_CHUNK;
        if (send(r->priv_fd, src + sent, chunk, 0) != (ssize_t)chunk)
            return -errno;
        sent += chunk;
        uint32_t ack;
        err = recv_u32_priv(r, &ack);
        if (err) return err;
        if (ack != 0) {
            r->last_err = (int)ack;
            return -EIO;
        }
    }
    return 0;
}
```

**Step 2: Build and commit**

```bash
cmake --build /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer/build/ 2>&1 | tail -5
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer \
    add librazerd/librazerd.c
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer \
    commit -m "Add librazerd privileged firmware flash operation"
```

---

## Task 12: Notification thread (LIBRAZERD_NOTIFICATIONS)

**Files:**
- Modify: `librazerd/librazerd.c`
- Modify: `librazerd/test_librazerd.c`

**Step 1: Replace the stub `notify_thread_fn` with the real implementation**

Find the stub (added in Task 3) and replace it:

```c
#ifdef LIBRAZERD_NOTIFICATIONS

static void notify_enqueue(razerd_t *r, razerd_event_type_t type)
{
    mtx_lock(&r->notify_lock);
    size_t next = (r->nq_tail + 1u) % NOTIFY_QUEUE_SIZE;
    if (next != r->nq_head) {          /* queue not full — drop oldest if full */
        r->nqueue[r->nq_tail].type = type;
        r->nq_tail = next;
    }
    mtx_unlock(&r->notify_lock);

    /* Signal the eventfd: readable → caller can call razerd_read_event */
    uint64_t val = 1;
    (void)write(r->notify_evfd, &val, sizeof(val));
}

static int notify_thread_fn(void *arg)
{
    razerd_t *r = arg;
    uint8_t pkt;

    while (!r->notify_stop) {
        ssize_t n = recv(r->notify_fd, &pkt, 1, 0);
        if (n <= 0)
            break;  /* socket shut down or error → stop */

        if (pkt == NOTIFY_NEWMOUSE)
            notify_enqueue(r, RAZERD_EVENT_NEW_MOUSE);
        else if (pkt == NOTIFY_DELMOUSE)
            notify_enqueue(r, RAZERD_EVENT_DEL_MOUSE);
        /* Unexpected reply types on the notify socket are ignored */
    }
    return 0;
}

int razerd_get_notify_fd(razerd_t *r)
{
    return r->notify_evfd;
}

int razerd_read_event(razerd_t *r, razerd_event_t *ev_out)
{
    mtx_lock(&r->notify_lock);
    if (r->nq_head == r->nq_tail) {
        mtx_unlock(&r->notify_lock);
        return -EAGAIN;
    }
    *ev_out = r->nqueue[r->nq_head];
    r->nq_head = (r->nq_head + 1u) % NOTIFY_QUEUE_SIZE;
    mtx_unlock(&r->notify_lock);

    /* Drain one count from the eventfd so it becomes unreadable when
     * the queue is empty again. */
    uint64_t val;
    (void)read(r->notify_evfd, &val, sizeof(val));

    return 0;
}

#endif /* LIBRAZERD_NOTIFICATIONS */
```

**Step 2: Add notification test to `test_librazerd.c`**

```c
#ifdef LIBRAZERD_NOTIFICATIONS
#include <poll.h>
static void test_notifications(void)
{
    TEST("razerd notifications (poll fd)");
    razerd_t *r = razerd_open();
    ASSERT(r != NULL, "open failed");

    int fd = razerd_get_notify_fd(r);
    ASSERT(fd >= 0, "notify fd invalid");

    /* Poll with 0 timeout — should report no events right now */
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    int ret = poll(&pfd, 1, 0);
    ASSERT(ret == 0, "unexpected event on notify fd");

    printf("(fd=%d, no spurious events) ", fd);
    razerd_close(r);
    PASS();
}
#endif
```

Add `#ifdef LIBRAZERD_NOTIFICATIONS test_notifications(); #endif` in `main`.

**Step 3: Build and run**

```bash
cmake --build /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer/build/ 2>&1 | tail -5
cd /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer/build/librazerd && ./test_librazerd
```
Expected: all tests pass, including `razerd notifications (poll fd)`.

**Step 4: Commit**

```bash
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer \
    add librazerd/librazerd.c librazerd/test_librazerd.c
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer \
    commit -m "Add librazerd async notification thread with eventfd"
```

---

## Task 13: Final build, sanitizer run, install test, push

**Files:** None (verification only)

**Step 1: Full clean debug build (sanitizers enabled)**

```bash
cd /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer
cmake -DCMAKE_BUILD_TYPE=Debug -B build_debug
cmake --build build_debug 2>&1 | grep -E "error:|warning:|Built target"
```
Expected: no errors, no warnings, all targets built.

**Step 2: Run all tests under sanitizers**

```bash
cd build_debug/librazerd
./test_librazerd
```
Expected: all tests pass, no sanitizer reports (AddressSanitizer/UBSan output would appear here if issues exist).

**Step 3: Verify `librazerd.so` is built**

```bash
ls -lh build_debug/librazerd/librazerd.so*
file build_debug/librazerd/librazerd.so
nm -D build_debug/librazerd/librazerd.so | grep "razerd_" | head -20
```
Expected: `librazerd.so` is a shared ELF library; `razerd_open`, `razerd_close`, `razerd_get_mice`, etc. are all exported symbols.

**Step 4: Install and verify**

```bash
sudo make -C build_debug install
ls /usr/local/lib/librazerd.so* /usr/local/include/librazerd.h
sudo ldconfig /usr/local/lib
```
Expected: `librazerd.so`, `librazerd.so.1` symlink, and `librazerd.h` present.

**Step 5: Push to GitHub**

```bash
git -C /home/blubskye/Downloads/tor-browser-linux-x86_64-15.0.7/razer push github master
```

---

## Notes for the implementer

- **`EREMOTEIO`**: if this errno constant doesn't exist on the build system (check with `grep -r EREMOTEIO /usr/include`), replace every occurrence with `-EIO`.
- **`[[nodiscard]]`**: requires GCC 11+ or Clang 13+ in C mode with `-std=c2x`. If the compiler rejects it, fall back to `__attribute__((warn_unused_result))`.
- **Notification socket demultiplexing**: the `notify_sock_fd` is a separate connection to `razerd`. After the GETREV handshake, only notification packets (bytes ≥ 128) should arrive. If `razerd` ever sends spurious replies on this connection, the notification thread will ignore them (the `else` branch is a no-op).
- **Thread safety of `priv_fd`**: `razerd_flash_firmware` currently does not hold a lock when using `priv_fd`. This is safe as long as only one thread calls `razerd_flash_firmware` at a time. For robust concurrent use, add a `priv_lock` field to `struct razerd` (same pattern as `lock`).
