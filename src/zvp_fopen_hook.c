#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__arm__) && !defined(ZVP_HOST_TEST)
__asm__(".symver dlsym, dlsym@GLIBC_2.4");
#endif

#if !defined(ZVP_HOST_TEST) && !defined(_WIN32)
extern void zvp_logf(const char *fmt, ...);
#endif

extern bool zvp_sound_has_active_remap(void);
extern const char *zvp_sound_remap_fopen_path(const char *path);
extern bool zvp_runtime_is_bypassed(void);

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
    if (idx < 256u) {
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
    if (zvp_runtime_is_bypassed()) {
        return fn ? fn(path, mode) : NULL;
    }
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
    if (zvp_runtime_is_bypassed()) {
        return fn ? fn(path, mode) : NULL;
    }
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
typedef void (*zvp_fn_setseqid_t)(void *self, unsigned short mid, unsigned int seq);
static volatile unsigned int g_osalsnd_diag_count = 0;
static void *volatile g_osalsnd_cmn_self = NULL;
static volatile unsigned int g_osalsnd_0201_msg_id = 0u;
static unsigned char g_osalsnd_0201_hdr[8] = { 0 };
static volatile int g_osalsnd_0201_ready = 0;

bool zvp_sound_hook_unregist_adf(uint8_t adf_id)
{
#if !defined(ZVP_HOST_TEST) && !defined(_WIN32)
    static zvp_fn_osalsnd_t real_fn = NULL;
    static zvp_fn_setseqid_t setseq_fn = NULL;
    void *cmn = __atomic_load_n(&g_osalsnd_cmn_self, __ATOMIC_ACQUIRE);
    unsigned int msg_id = __atomic_load_n(&g_osalsnd_0201_msg_id, __ATOMIC_ACQUIRE);
    if (!cmn || __atomic_load_n(&g_osalsnd_0201_ready, __ATOMIC_ACQUIRE) == 0 || adf_id == 0u || adf_id > 20u) {
        return false;
    }
    zvp_fn_osalsnd_t fn = __atomic_load_n(&real_fn, __ATOMIC_ACQUIRE);
    if (!fn) {
        fn = (zvp_fn_osalsnd_t)dlsym(RTLD_NEXT, "_ZN11ObjAvbbcCmn10OsalSndMsgEjPKhj");
        if (fn) {
            __atomic_store_n(&real_fn, fn, __ATOMIC_RELEASE);
        }
    }
    if (!fn) {
        return false;
    }
    zvp_fn_setseqid_t sfn = __atomic_load_n(&setseq_fn, __ATOMIC_ACQUIRE);
    if (!sfn) {
        sfn = (zvp_fn_setseqid_t)dlsym(RTLD_DEFAULT, "_ZN11ObjAvbbcCmn8SetSeqIdEtj");
        if (sfn) {
            __atomic_store_n(&setseq_fn, sfn, __ATOMIC_RELEASE);
        }
    }
    unsigned char pkt[36];
    for (unsigned int i = 0u; i < 36u; ++i) {
        pkt[i] = 0u;
    }
    for (unsigned int i = 0u; i < 8u; ++i) {
        pkt[i] = g_osalsnd_0201_hdr[i];
    }
    pkt[24] = adf_id;
    pkt[26] = 0u; /* SetTableUnregist */
    if (sfn) {
        sfn(cmn, 0x8209u, 0u);
    }
    int rc = fn(cmn, msg_id, pkt, 36u);
    __atomic_fetch_and(&g_zvp_adf_mask, ~(1u << adf_id), __ATOMIC_ACQ_REL);
    zvp_logf("osal_snd 主动卸载旧 adf=0x%02x (0x8209 reg=0) rc=%d", (unsigned)adf_id, rc);
    return rc >= 0;
#else
    (void)adf_id;
    return false;
#endif
}

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
    if (zvp_runtime_is_bypassed()) {
        return fn ? fn(self, msg_id, data, len) : -1;
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
            if (self && data[26] == 1u) {
                __atomic_store_n(&g_osalsnd_cmn_self, self, __ATOMIC_RELEASE);
                __atomic_store_n(&g_osalsnd_0201_msg_id, msg_id, __ATOMIC_RELEASE);
                for (unsigned int i = 0u; i < 8u; ++i) {
                    g_osalsnd_0201_hdr[i] = data[i];
                }
                __atomic_store_n(&g_osalsnd_0201_ready, 1, __ATOMIC_RELEASE);
            }
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
    if (data && ((msg_id & 0x0fffffffu) == 0x0058003fu ||
                 (msg_id & 0x0fffffffu) == 0x0061003cu) && len >= 4u) {
        unsigned int mid_low = (msg_id & 0x0fffffffu);
        unsigned int cmd = ((unsigned int)send_data[3] << 8) | send_data[2];
        if (mid_low == 0x0061003cu || (cmd & 0xFF00u) == 0x0200u ||
            (cmd & 0xFF00u) == 0x0400u || is_zvp_msg) {
            if (cmd != 0x0424u) {
                unsigned int idx = __atomic_fetch_add(&g_osalsnd_diag_count, 1u, __ATOMIC_RELAXED);
                if (idx < 256u) {
                    if (mid_low == 0x0061003cu) {
                        unsigned int pool = 0u;
                        unsigned int hdl = 0u;
                        if (len >= 8u) {
                            pool = (unsigned int)send_data[4] | ((unsigned int)send_data[5] << 8) |
                                   ((unsigned int)send_data[6] << 16) | ((unsigned int)send_data[7] << 24);
                        }
                        if (len >= 4u) {
                            hdl = (unsigned int)send_data[2] | ((unsigned int)send_data[3] << 8);
                        }
                        zvp_logf("osal_snd #%u MEM(0x61003c) d0..3=%02x%02x_%04x len=%u p4=%08x rc=%d",
                                 idx, send_data[0], send_data[1], hdl, len, pool, rc);
                    } else if (fixed_vol) {
                        zvp_logf("osal_snd #%u 解除静音 adf=0x%02x vol %u->7 rc=%d",
                                 idx, (unsigned)fixed_adf, prev_vol, rc);
                    } else if (len == 36u && cmd == 0x0201u) {
                        zvp_logf("osal_snd #%u 0201(AdfTbl) zvp=%d adf=0x%02x reg=%u sz=%u rc=%d",
                                 idx, is_zvp_msg ? 1 : 0, (unsigned)send_data[24],
                                 (unsigned)send_data[26],
                                 (unsigned)(send_data[20] | ((unsigned)send_data[21] << 8) |
                                            ((unsigned)send_data[22] << 16) | ((unsigned)send_data[23] << 24)),
                                 rc);
                    } else if (len == 16u && cmd == 0x0203u) {
                        zvp_logf("osal_snd #%u 0203(Play) zvp=%d adf=0x%02x rc=%d",
                                 idx, is_zvp_msg ? 1 : 0, (unsigned)send_data[10], rc);
                    } else if (len == 12u && cmd == 0x0204u) {
                        zvp_logf("osal_snd #%u 0204(Vol) zvp=%d adf=0x%02x vol=%u rc=%d",
                                 idx, is_zvp_msg ? 1 : 0, (unsigned)send_data[9], (unsigned)send_data[8], rc);
                    } else {
                        zvp_logf("osal_snd #%u mid=0x%08x cmd=0x%04x len=%u d0..7=%02x%02x%02x%02x_%02x%02x%02x%02x rc=%d",
                                 idx, msg_id, cmd, len,
                                 send_data[0], send_data[1], send_data[2], send_data[3],
                                 len > 4 ? send_data[4] : 0, len > 5 ? send_data[5] : 0,
                                 len > 6 ? send_data[6] : 0, len > 7 ? send_data[7] : 0, rc);
                    }
                }
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
