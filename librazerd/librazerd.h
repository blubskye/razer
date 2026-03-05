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
    bool     is_mutable;   /* renamed from 'mutable' for C++ compatibility */
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
[[nodiscard]] int razerd_get_notify_fd(razerd_t *r);

/* Non-blocking dequeue. Returns 0 and fills *ev on success,
 * -EAGAIN if queue is empty. */
[[nodiscard]] int razerd_read_event(razerd_t *r, razerd_event_t *ev_out);
#endif /* LIBRAZERD_NOTIFICATIONS */
