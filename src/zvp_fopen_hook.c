#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdbool.h>
#include <stddef.h>

#if defined(__arm__) && !defined(ZVP_HOST_TEST)
__asm__(".symver dlsym, dlsym@GLIBC_2.4");
#endif

#if !defined(ZVP_HOST_TEST) && !defined(_WIN32)
extern void zvp_logf(const char *fmt, ...);
#endif

extern bool zvp_sound_has_active_remap(void);
extern const char *zvp_sound_remap_fopen_path(const char *path);

/* 不包含 <stdio.h>，避免 glibc 的 __REDIRECT 将 fopen 改名为 fopen64 */
typedef void *(*zvp_fn_fopen_t)(const char *path, const char *mode);

static __thread int s_in_fopen_resolve = 0;
static volatile unsigned int g_fopen_diag_count = 0;
static volatile unsigned int g_zvp_pending_adf_load = 0;
static volatile unsigned int g_zvp_adf_mask = 0;

void zvp_sound_hook_reset(void)
{
    __atomic_store_n(&g_zvp_pending_adf_load, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&g_zvp_adf_mask, 0u, __ATOMIC_RELEASE);
}

static bool path_has_substr(const char *s, const char *needle)
{
    if (!s || !needle || !needle[0]) {
        return false;
    }
    for (size_t i = 0; s[i] != '\0'; ++i) {
        size_t j = 0;
        while (needle[j] != '\0' && s[i + j] == needle[j]) {
            ++j;
        }
        if (needle[j] == '\0') {
            return true;
        }
    }
    return false;
}

static bool is_sound_diag_path(const char *path)
{
    return path_has_substr(path, "avsystemsound") || path_has_substr(path, "zvp_slot_");
}

static void log_sound_fopen(const char *tag, const char *path, const char *actual,
                            const char *mode, void *fp)
{
#if !defined(ZVP_HOST_TEST) && !defined(_WIN32)
    if (!is_sound_diag_path(path) && !is_sound_diag_path(actual)) {
        return;
    }
    unsigned int idx = __atomic_fetch_add(&g_fopen_diag_count, 1u, __ATOMIC_RELAXED);
    if (idx < 48u) {
        zvp_logf("fopen[%s] #%u remap=%d mode=%s path=%s actual=%s fp=%p",
                 tag, idx, zvp_sound_has_active_remap() ? 1 : 0,
                 mode ? mode : "(null)",
                 path ? path : "(null)",
                 actual ? actual : "(null)",
                 fp);
    }
#else
    (void)tag;
    (void)path;
    (void)actual;
    (void)mode;
    (void)fp;
#endif
}

static zvp_fn_fopen_t resolve_next_fopen(zvp_fn_fopen_t *cache, const char *sym)
{
    zvp_fn_fopen_t fn = __atomic_load_n(cache, __ATOMIC_ACQUIRE);
    if (!fn) {
        if (s_in_fopen_resolve) {
            return NULL;
        }
        s_in_fopen_resolve = 1;
        fn = (zvp_fn_fopen_t)dlsym(RTLD_NEXT, sym);
        s_in_fopen_resolve = 0;
        if (fn) {
            __atomic_store_n(cache, fn, __ATOMIC_RELEASE);
        }
    }
    return fn;
}

void *fopen(const char *path, const char *mode)
{
    static zvp_fn_fopen_t real_fn = NULL;
    zvp_fn_fopen_t fn = resolve_next_fopen(&real_fn, "fopen");
    bool remap = zvp_sound_has_active_remap();
    const char *actual = remap ? zvp_sound_remap_fopen_path(path) : path;
    void *fp = fn ? fn(actual, mode) : NULL;
    if (remap && path && actual != path && fp != NULL) {
        __atomic_store_n(&g_zvp_pending_adf_load, 1u, __ATOMIC_RELEASE);
    }
    log_sound_fopen("fopen", path, actual, mode, fp);
    return fp;
}

void *fopen64(const char *path, const char *mode)
{
    static zvp_fn_fopen_t real_fn = NULL;
    zvp_fn_fopen_t fn = resolve_next_fopen(&real_fn, "fopen64");
    bool remap = zvp_sound_has_active_remap();
    const char *actual = remap ? zvp_sound_remap_fopen_path(path) : path;
    void *fp = fn ? fn(actual, mode) : NULL;
    if (remap && path && actual != path && fp != NULL) {
        __atomic_store_n(&g_zvp_pending_adf_load, 1u, __ATOMIC_RELEASE);
    }
    log_sound_fopen("fopen64", path, actual, mode, fp);
    return fp;
}

typedef int (*zvp_fn_osalsnd_t)(void *self, unsigned int msg_id, const unsigned char *data, unsigned int len);
static volatile unsigned int g_osalsnd_diag_count = 0;

int _ZN11ObjAvbbcCmn10OsalSndMsgEjPKhj(void *self, unsigned int msg_id,
                                        const unsigned char *data, unsigned int len)
{
    static zvp_fn_osalsnd_t real_fn = NULL;
    zvp_fn_osalsnd_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (zvp_fn_osalsnd_t)dlsym(RTLD_NEXT, "_ZN11ObjAvbbcCmn10OsalSndMsgEjPKhj");
        if (fn) {
            __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
        }
    }

    unsigned char patched[16];
    const unsigned char *send_data = data;
    unsigned int prev_vol = 0u;
    bool fixed_vol = false;
    unsigned char fixed_adf = 0u;
    bool is_zvp_msg = false;

    if (data && (msg_id & 0x0fffffffu) == 0x0058003fu) {
        if (len == 36u && data[0] == 0x04u && data[1] == 0x20u &&
            data[2] == 0x01u && data[3] == 0x02u) {
            /* 0x8209 (0x0201) AdfSetTable: 记录自定义槽位分配到的 adf_id，非自定义则清除 */
            unsigned char adf_id = data[24];
            if (adf_id < 32u) {
                bool is_zvp = (__atomic_exchange_n(&g_zvp_pending_adf_load, 0u, __ATOMIC_ACQ_REL) != 0u);
                if (is_zvp) {
                    __atomic_fetch_or(&g_zvp_adf_mask, 1u << adf_id, __ATOMIC_ACQ_REL);
                    is_zvp_msg = true;
                } else {
                    __atomic_fetch_and(&g_zvp_adf_mask, ~(1u << adf_id), __ATOMIC_ACQ_REL);
                }
            }
        } else if (len == 12u && data[0] == 0x04u && data[1] == 0x20u &&
                   data[2] == 0x04u && data[3] == 0x02u) {
            /* 0x820c (0x0204) SetPerSoundVolume: 将 Backup 默认静音 (0x00) 覆盖为满音量 (0x07) */
            unsigned char adf_id = data[9];
            unsigned int mask = __atomic_load_n(&g_zvp_adf_mask, __ATOMIC_ACQUIRE);
            if (adf_id < 32u && (mask & (1u << adf_id)) != 0u) {
                is_zvp_msg = true;
                if (data[8] < 7u) {
                    for (unsigned int i = 0u; i < 12u; ++i) {
                        patched[i] = data[i];
                    }
                    prev_vol = patched[8];
                    patched[8] = 0x07u;
                    send_data = patched;
                    fixed_vol = true;
                    fixed_adf = adf_id;
                }
            }
        } else if (len == 16u && data[0] == 0x04u && data[1] == 0x20u &&
                   data[2] == 0x03u && data[3] == 0x02u) {
            unsigned char adf_id = data[10];
            unsigned int mask = __atomic_load_n(&g_zvp_adf_mask, __ATOMIC_ACQUIRE);
            if (adf_id < 32u && (mask & (1u << adf_id)) != 0u) {
                is_zvp_msg = true;
            }
        }
    }

    int rc = fn ? fn(self, msg_id, send_data, len) : -1;
#if !defined(ZVP_HOST_TEST) && !defined(_WIN32)
    if (is_zvp_msg) {
        unsigned int idx = __atomic_fetch_add(&g_osalsnd_diag_count, 1u, __ATOMIC_RELAXED);
        if (idx < 24u) {
            if (fixed_vol) {
                zvp_logf("osal_snd #%u 解除静音 adf=0x%02x vol %u->7 rc=%d",
                         idx, (unsigned)fixed_adf, prev_vol, rc);
            } else {
                zvp_logf("osal_snd #%u zvp cmd=0x%02x%02x len=%u rc=%d",
                         idx, (unsigned)send_data[3], (unsigned)send_data[2], len, rc);
            }
        }
    }
#else
    (void)prev_vol;
    (void)fixed_vol;
    (void)fixed_adf;
#endif
    return rc;
}
