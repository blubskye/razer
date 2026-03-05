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
    _Atomic bool    notify_stop;
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
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
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
#endif
    mtx_destroy(&r->lock);
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

/* ------------------------------------------------------------------ */
/* Protocol helpers                                                    */
/* ------------------------------------------------------------------ */

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
static __attribute__((unused)) int send_priv_cmd(razerd_t *r, uint8_t cmd_id,
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

/* Receive a REPLY_U32 while holding r->lock.
 * Caller must hold r->lock before calling. */
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
static __attribute__((unused)) int recv_u32_priv(razerd_t *r, uint32_t *out)
{
    if (r->priv_fd < 0) return -EPERM;
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
        uint8_t *raw = malloc(nbytes ? nbytes : 1);
        if (!raw) return -ENOMEM;
        if (nbytes) {
            err = recv_all(r->cmd_fd, raw, nbytes);
            if (err) { free(raw); return err; }
        }

        /* Convert BMP UTF-16-BE -> UTF-8.
         * Each UTF-16 code unit -> up to 3 UTF-8 bytes. */
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

/* Same but reads from priv_fd. */
static __attribute__((unused)) int recv_str_priv(razerd_t *r, char **out)
{
    if (r->priv_fd < 0) return -EPERM;
    uint8_t hdr;
    int err = recv_all(r->priv_fd, &hdr, 1);
    if (err) return err;
    if (hdr != REPLY_STR) return -EPROTO;
    uint8_t enc;
    err = recv_all(r->priv_fd, &enc, 1);
    if (err) return err;
    uint16_t slen_be;
    err = recv_all(r->priv_fd, (uint8_t *)&slen_be, 2);
    if (err) return err;
    uint16_t slen = be16toh(slen_be);
    if (enc == STR_ENC_UTF16BE) {
        size_t nbytes = (size_t)slen * 2u;
        uint8_t *raw = malloc(nbytes ? nbytes : 1);
        if (!raw) return -ENOMEM;
        if (nbytes) {
            err = recv_all(r->priv_fd, raw, nbytes);
            if (err) { free(raw); return err; }
        }
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
    char *buf = malloc((size_t)slen + 1u);
    if (!buf) return -ENOMEM;
    if (slen) {
        err = recv_all(r->priv_fd, buf, slen);
        if (err) { free(buf); return err; }
    }
    buf[slen] = '\0';
    *out = buf;
    return 0;
}

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

/* ------------------------------------------------------------------ */
/* LED stubs                                                           */
/* ------------------------------------------------------------------ */

int razerd_get_leds(razerd_t *r, const char *idstr, uint32_t profile_id,
                    razerd_led_t **leds_out, size_t *count_out)
{
    (void)r; (void)idstr; (void)profile_id;
    *leds_out  = NULL;
    *count_out = 0;
    return -ENOSYS;
}

void razerd_free_leds(razerd_led_t *leds)
{
    free(leds);
}

int razerd_set_led(razerd_t *r, const char *idstr, uint32_t profile_id,
                   const razerd_led_t *led)
{
    (void)r; (void)idstr; (void)profile_id; (void)led;
    return -ENOSYS;
}

/* ------------------------------------------------------------------ */
/* DPI stubs                                                           */
/* ------------------------------------------------------------------ */

int razerd_get_dpi_mappings(razerd_t *r, const char *idstr,
                             razerd_dpi_mapping_t **out, size_t *count_out)
{
    (void)r; (void)idstr;
    *out       = NULL;
    *count_out = 0;
    return -ENOSYS;
}

void razerd_free_dpi_mappings(razerd_dpi_mapping_t *m)
{
    free(m);
}

int razerd_get_dpi_mapping(razerd_t *r, const char *idstr,
                            uint32_t profile_id, uint32_t axis_id,
                            uint32_t *mapping_id_out)
{
    (void)r; (void)idstr; (void)profile_id; (void)axis_id;
    *mapping_id_out = 0;
    return -ENOSYS;
}

int razerd_set_dpi_mapping(razerd_t *r, const char *idstr,
                            uint32_t profile_id, uint32_t mapping_id,
                            uint32_t axis_id)
{
    (void)r; (void)idstr; (void)profile_id; (void)mapping_id; (void)axis_id;
    return -ENOSYS;
}

int razerd_change_dpi_mapping(razerd_t *r, const char *idstr,
                               uint32_t mapping_id, uint32_t dim_id,
                               uint32_t new_res)
{
    (void)r; (void)idstr; (void)mapping_id; (void)dim_id; (void)new_res;
    return -ENOSYS;
}

/* ------------------------------------------------------------------ */
/* Frequency stubs                                                     */
/* ------------------------------------------------------------------ */

int razerd_get_supported_freqs(razerd_t *r, const char *idstr,
                                uint32_t **out, size_t *count_out)
{
    (void)r; (void)idstr;
    *out       = NULL;
    *count_out = 0;
    return -ENOSYS;
}

void razerd_free_freqs(uint32_t *out)
{
    free(out);
}

int razerd_get_freq(razerd_t *r, const char *idstr,
                    uint32_t profile_id, uint32_t *freq_out)
{
    (void)r; (void)idstr; (void)profile_id;
    *freq_out = 0;
    return -ENOSYS;
}

int razerd_set_freq(razerd_t *r, const char *idstr,
                    uint32_t profile_id, uint32_t freq)
{
    (void)r; (void)idstr; (void)profile_id; (void)freq;
    return -ENOSYS;
}

/* ------------------------------------------------------------------ */
/* Firmware stubs                                                      */
/* ------------------------------------------------------------------ */

int razerd_get_fw_version(razerd_t *r, const char *idstr,
                           uint8_t *major_out, uint8_t *minor_out)
{
    (void)r; (void)idstr;
    *major_out = 0;
    *minor_out = 0;
    return -ENOSYS;
}

/* ------------------------------------------------------------------ */
/* Profile stubs                                                       */
/* ------------------------------------------------------------------ */

int razerd_get_profiles(razerd_t *r, const char *idstr,
                         uint32_t **ids_out, size_t *count_out)
{
    (void)r; (void)idstr;
    *ids_out   = NULL;
    *count_out = 0;
    return -ENOSYS;
}

void razerd_free_profiles(uint32_t *ids)
{
    free(ids);
}

int razerd_get_active_profile(razerd_t *r, const char *idstr,
                               uint32_t *id_out)
{
    (void)r; (void)idstr;
    *id_out = RAZERD_PROFILE_INVALID;
    return -ENOSYS;
}

int razerd_set_active_profile(razerd_t *r, const char *idstr, uint32_t id)
{
    (void)r; (void)idstr; (void)id;
    return -ENOSYS;
}

int razerd_get_profile_name(razerd_t *r, const char *idstr,
                             uint32_t profile_id, char **name_out)
{
    (void)r; (void)idstr; (void)profile_id;
    *name_out = NULL;
    return -ENOSYS;
}

int razerd_set_profile_name(razerd_t *r, const char *idstr,
                             uint32_t profile_id, const char *name)
{
    (void)r; (void)idstr; (void)profile_id; (void)name;
    return -ENOSYS;
}

/* ------------------------------------------------------------------ */
/* Button stubs                                                        */
/* ------------------------------------------------------------------ */

int razerd_get_buttons(razerd_t *r, const char *idstr,
                        razerd_button_t **out, size_t *count_out)
{
    (void)r; (void)idstr;
    *out       = NULL;
    *count_out = 0;
    return -ENOSYS;
}

void razerd_free_buttons(razerd_button_t *b)
{
    free(b);
}

int razerd_get_button_functions(razerd_t *r, const char *idstr,
                                 razerd_button_func_t **out, size_t *count_out)
{
    (void)r; (void)idstr;
    *out       = NULL;
    *count_out = 0;
    return -ENOSYS;
}

void razerd_free_button_functions(razerd_button_func_t *f)
{
    free(f);
}

int razerd_get_button_function(razerd_t *r, const char *idstr,
                                uint32_t profile_id, uint32_t button_id,
                                razerd_button_func_t *out)
{
    (void)r; (void)idstr; (void)profile_id; (void)button_id; (void)out;
    return -ENOSYS;
}

int razerd_set_button_function(razerd_t *r, const char *idstr,
                                uint32_t profile_id, uint32_t button_id,
                                uint32_t func_id)
{
    (void)r; (void)idstr; (void)profile_id; (void)button_id; (void)func_id;
    return -ENOSYS;
}

/* ------------------------------------------------------------------ */
/* Axes stubs                                                          */
/* ------------------------------------------------------------------ */

int razerd_get_axes(razerd_t *r, const char *idstr,
                    razerd_axis_t **out, size_t *count_out)
{
    (void)r; (void)idstr;
    *out       = NULL;
    *count_out = 0;
    return -ENOSYS;
}

void razerd_free_axes(razerd_axis_t *a)
{
    free(a);
}

/* ------------------------------------------------------------------ */
/* Privileged operations stubs                                        */
/* ------------------------------------------------------------------ */

int razerd_flash_firmware(razerd_t *r, const char *idstr,
                           const void *image, size_t len)
{
    (void)r; (void)idstr; (void)image; (void)len;
    return -ENOSYS;
}

/* ------------------------------------------------------------------ */
/* Notification thread                                                 */
/* ------------------------------------------------------------------ */

#ifdef LIBRAZERD_NOTIFICATIONS
static int notify_thread_fn(void *arg)
{
    razerd_t *r = arg;
    uint8_t pkt;
    while (!r->notify_stop) {
        ssize_t n = recv(r->notify_fd, &pkt, 1, 0);
        if (n <= 0) break;
        /* TODO: implement in Task 12 */
        (void)pkt;
    }
    return 0;
}

int razerd_get_notify_fd(razerd_t *r)
{
    return r->notify_evfd;
}

int razerd_read_event(razerd_t *r, razerd_event_t *ev_out)
{
    (void)ev_out;
    mtx_lock(&r->notify_lock);
    if (r->nq_head == r->nq_tail) {
        mtx_unlock(&r->notify_lock);
        return -EAGAIN;
    }
    *ev_out = r->nqueue[r->nq_head % NOTIFY_QUEUE_SIZE];
    r->nq_head++;
    mtx_unlock(&r->notify_lock);
    return 0;
}
#endif
