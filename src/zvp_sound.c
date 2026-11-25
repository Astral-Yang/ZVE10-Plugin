#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "zvp_sound.h"
#include "zvp_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(ZVP_HOST_TEST) && !defined(_WIN32)
#define sound_logf(...) zvp_logf(__VA_ARGS__)
#else
#define sound_logf(...) ((void)0)
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/* ============================================================================
 * 1. Q24 Blackman-windowed Sinc 核（无 libm 依赖，源自 Sony F0 工具链）
 * ========================================================================== */

static const int32_t k_sinc_q24[769] = {
    13421773,13418231,13407610,13389920,13365179,13333410,13294645,13248921,
    13196285,13136786,13070485,12997444,12917737,12831441,12738639,12639423,
    12533890,12422141,12304286,12180439,12050720,11915255,11774174,11627614,
    11475716,11318626,11156494,10989476,10817731,10641423,10460719,10275792,
    10086815,9893968,9697430,9497388,9294027,9087538,8878112,8665943,
    8451226,8234159,8014939,7793768,7570846,7346373,7120552,6893584,
    6665671,6437016,6207819,5978280,5748601,5518977,5289608,5060689,
    4832413,4604972,4378555,4153350,3929542,3707311,3486837,3268296,
    3051859,2837695,2625969,2416842,2210471,2007009,1806604,1609401,
    1415538,1225151,1038369,855316,676112,500873,329706,162716,
    0,-158348,-312241,-461599,-606345,-746409,-881727,-1012242,
    -1137899,-1258654,-1374465,-1485298,-1591123,-1691917,-1787664,-1878351,
    -1963973,-2044530,-2120028,-2190477,-2255895,-2316304,-2371732,-2422211,
    -2467779,-2508481,-2544364,-2575482,-2601892,-2623658,-2640847,-2653530,
    -2661783,-2665687,-2665325,-2660787,-2652162,-2639548,-2623042,-2602747,
    -2578768,-2551212,-2520191,-2485818,-2448208,-2407480,-2363754,-2317151,
    -2267796,-2215814,-2161331,-2104475,-2045375,-1984161,-1920962,-1855911,
    -1789137,-1720772,-1650948,-1579795,-1507443,-1434024,-1359666,-1284497,
    -1208647,-1132239,-1055401,-978254,-900922,-823523,-746177,-669000,
    -592105,-515605,-439608,-364222,-289551,-215696,-142756,-70826,
    0,69634,137988,204981,270531,334563,397004,457784,
    516838,574103,629521,683036,734599,784160,831677,877109,
    920420,961578,1000553,1037320,1071859,1104152,1134184,1161945,
    1187428,1210630,1231551,1250195,1266569,1280683,1292551,1302190,
    1309620,1314864,1317948,1318900,1317754,1314542,1309303,1302075,
    1292902,1281827,1268898,1254162,1237672,1219479,1199638,1178207,
    1155242,1130804,1104952,1077750,1049261,1019548,988677,956715,
    923728,889783,854949,819293,782884,745791,708082,669826,
    631092,591948,552462,512700,472730,432619,392430,352230,
    312081,272046,232186,192563,153234,114258,75690,37587,
    0,-37018,-73418,-109150,-144169,-178430,-211889,-244507,
    -276243,-307061,-336926,-365805,-393666,-420481,-446223,-470866,
    -494390,-516772,-537994,-558041,-576898,-594552,-610995,-626217,
    -640213,-652979,-664514,-674817,-683891,-691740,-698369,-703786,
    -708002,-711027,-712874,-713559,-713097,-711508,-708810,-705024,
    -700173,-694281,-687374,-679476,-670617,-660825,-650129,-638560,
    -626151,-612934,-598942,-584210,-568772,-552664,-535922,-518583,
    -500683,-482260,-463351,-443994,-424227,-404088,-383615,-362846,
    -341819,-320573,-299143,-277568,-255885,-234130,-212339,-190548,
    -168792,-147105,-125522,-104076,-82798,-61722,-40876,-20293,
    0,19974,39602,58857,77714,96149,114139,131662,
    148697,165223,181222,196677,211571,225889,239616,252740,
    265250,277134,288383,298989,308945,318246,326886,334863,
    342173,348816,354792,360101,364745,368729,372055,374729,
    376758,378148,378907,379045,378570,377495,375831,373589,
    370783,367426,363534,359121,354202,348795,342916,336582,
    329812,322623,315034,307065,298734,290061,281066,271769,
    262190,252350,242270,231968,221467,210787,199948,188971,
    177876,166683,155412,144083,132716,121329,109943,98575,
    87245,75969,64766,53652,42645,31761,21015,10423,
    0,-10240,-20284,-30118,-39730,-49108,-58240,-67116,
    -75725,-84058,-92107,-99861,-107316,-114462,-121294,-127806,
    -133992,-139850,-145374,-150561,-155409,-159917,-164081,-167903,
    -171381,-174517,-177310,-179763,-181878,-183657,-185104,-186222,
    -187015,-187488,-187646,-187495,-187040,-186289,-185246,-183921,
    -182319,-180450,-178321,-175940,-173316,-170458,-167376,-164078,
    -160574,-156875,-152989,-148927,-144699,-140315,-135786,-131122,
    -126333,-121429,-116422,-111322,-106139,-100882,-95564,-90193,
    -84779,-79334,-73866,-68384,-62900,-57422,-51958,-46519,
    -41112,-35746,-30430,-25171,-19977,-14856,-9815,-4861,
    0,4761,9416,13960,18386,22690,26868,30913,
    34823,38593,42220,45701,49032,52212,55237,58106,
    60817,63369,65761,67992,70062,71970,73717,75303,
    76729,77995,79104,80055,80852,81496,81989,82334,
    82533,82590,82506,82286,81932,81448,80839,80106,
    79256,78291,77217,76036,74755,73376,71905,70346,
    68705,66985,65192,63330,61404,59419,57380,55292,
    53159,50986,48778,46539,44275,41990,39687,37373,
    35051,32725,30400,28079,25768,23468,21186,18923,
    16684,14471,12289,10141,8029,5956,3925,1939,
    0,-1890,-3727,-5512,-7241,-8912,-10525,-12078,
    -13569,-14997,-16362,-17662,-18897,-20066,-21169,-22205,
    -23175,-24078,-24914,-25684,-26387,-27025,-27598,-28106,
    -28550,-28932,-29252,-29511,-29710,-29851,-29935,-29963,
    -29937,-29858,-29728,-29549,-29321,-29048,-28731,-28371,
    -27970,-27531,-27055,-26545,-26001,-25427,-24824,-24194,
    -23539,-22861,-22162,-21445,-20710,-19959,-19196,-18421,
    -17637,-16845,-16047,-15245,-14441,-13635,-12830,-12028,
    -11229,-10436,-9650,-8871,-8102,-7344,-6597,-5864,
    -5144,-4440,-3751,-3079,-2425,-1789,-1173,-576,
    0,555,1090,1602,2092,2560,3005,3428,
    3828,4205,4558,4889,5198,5483,5746,5987,
    6206,6403,6579,6734,6868,6983,7078,7153,
    7211,7250,7272,7278,7267,7241,7200,7145,
    7076,6995,6902,6797,6682,6557,6422,6280,
    6129,5971,5807,5638,5463,5284,5101,4915,
    4726,4536,4344,4152,3959,3767,3575,3385,
    3197,3011,2828,2647,2470,2297,2128,1963,
    1803,1648,1498,1353,1214,1080,952,830,
    714,603,499,401,309,223,143,68,
    0,-63,-120,-172,-218,-259,-295,-326,
    -353,-375,-393,-407,-417,-423,-427,-426,
    -424,-418,-410,-400,-387,-373,-358,-341,
    -324,-305,-286,-266,-246,-226,-206,-186,
    -167,-148,-130,-113,-97,-82,-68,-55,
    -43,-33,-24,-17,-11,-6,-3,-1,
    0
};

static uint16_t rd_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int32_t decode_pcm_q31(const uint8_t *p, uint16_t bits)
{
    uint32_t u;
    int64_t v;
    if (bits == 8u) {
        v = ((int64_t)p[0] - 128) * 16777216LL;
    } else if (bits == 16u) {
        u = rd_le16(p);
        v = (u >= 32768u ? (int64_t)u - 65536 : (int64_t)u) * 65536LL;
    } else if (bits == 24u) {
        u = (uint32_t)rd_le16(p) | ((uint32_t)p[2] << 16);
        v = (u >= 8388608u ? (int64_t)u - 16777216 : (int64_t)u) * 256LL;
    } else {
        u = rd_le32(p);
        v = (u >= 0x80000000u ? (int64_t)u - 4294967296LL : (int64_t)u);
    }
    return (int32_t)v;
}

static int32_t clamp_i16_val(int64_t v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int32_t)v;
}

static int64_t mono_q31_at(const uint8_t *pcm,
                           const zvp_wav_format_t *fmt,
                           uint32_t frame_idx)
{
    const uint8_t *p = pcm + (size_t)frame_idx * fmt->block_align;
    int64_t s0 = decode_pcm_q31(p, fmt->bits_per_sample);
    if (fmt->channels == 1u) {
        return s0;
    }
    int64_t s1 = decode_pcm_q31(p + (fmt->bits_per_sample / 8u), fmt->bits_per_sample);
    return (s0 + s1) / 2;
}

static uint32_t kernel_at_q24(uint32_t dist_q24)
{
    uint64_t pos = ((uint64_t)dist_q24 * 64u) >> 24;
    uint32_t idx = (uint32_t)pos;
    if (idx >= 768u) {
        return (uint32_t)k_sinc_q24[768];
    }
    uint32_t frac = (uint32_t)((((uint64_t)dist_q24 * 64u) & 0xFFFFFFULL));
    int64_t a = k_sinc_q24[idx];
    int64_t b = k_sinc_q24[idx + 1u];
    return (uint32_t)(int32_t)(a + (((b - a) * (int64_t)frac) >> 24));
}

int zvp_wav_parse(const uint8_t *wav, size_t len, zvp_wav_format_t *out)
{
    zvp_wav_format_t f;
    size_t pos = 12u;
    bool have_fmt = false;
    bool have_data = false;

    if (!wav || !out || len < 12u || len > ZVP_SOUND_MAX_WAV_BYTES) {
        return -1;
    }
    if (memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0) {
        return -1;
    }
    if ((uint64_t)rd_le32(wav + 4) + 8ULL > (uint64_t)len) {
        return -1;
    }

    memset(&f, 0, sizeof(f));
    while (pos + 8u <= len) {
        uint32_t sz = rd_le32(wav + pos + 4u);
        size_t start = pos + 8u;
        size_t padded;
        if ((size_t)sz > len - start) {
            return -1;
        }
        padded = (size_t)sz + ((size_t)sz & 1u);
        if (padded > len - start) {
            padded = (size_t)sz;
        }
        if (memcmp(wav + pos, "fmt ", 4) == 0) {
            uint16_t audio_fmt;
            uint32_t byte_rate;
            if (have_fmt || sz < 16u) {
                return -1;
            }
            audio_fmt = rd_le16(wav + start);
            if (audio_fmt != 1u) {
                return -1;
            }
            f.channels = rd_le16(wav + start + 2u);
            f.sample_rate = rd_le32(wav + start + 4u);
            byte_rate = rd_le32(wav + start + 8u);
            f.block_align = rd_le16(wav + start + 12u);
            f.bits_per_sample = rd_le16(wav + start + 14u);
            if ((f.channels != 1u && f.channels != 2u) ||
                f.sample_rate < 8000u || f.sample_rate > 192000u ||
                (f.bits_per_sample != 8u && f.bits_per_sample != 16u &&
                 f.bits_per_sample != 24u && f.bits_per_sample != 32u) ||
                f.block_align != (uint16_t)(f.channels * (f.bits_per_sample / 8u)) ||
                byte_rate != f.sample_rate * f.block_align) {
                return -1;
            }
            have_fmt = true;
        } else if (memcmp(wav + pos, "data", 4) == 0) {
            if (!have_data && sz > 0u) {
                f.data_offset = (uint32_t)start;
                f.data_bytes = sz;
                have_data = true;
            }
        }
        pos = start + padded;
    }

    if (!have_fmt || !have_data || (f.data_bytes % f.block_align) != 0u) {
        return -1;
    }
    f.frames = f.data_bytes / f.block_align;
    if (f.frames == 0u ||
        (uint64_t)f.frames > (uint64_t)f.sample_rate * ZVP_SOUND_MAX_SECONDS) {
        return -1;
    }
    *out = f;
    return 0;
}

static size_t encode_i16_to_f0(const int16_t *samples,
                               size_t count,
                               uint8_t *out_f0,
                               size_t out_cap)
{
    size_t blocks = (count + ZVP_SOUND_F0_SAMPLES_PER_BLOCK - 1u) /
                    ZVP_SOUND_F0_SAMPLES_PER_BLOCK;
    if (blocks == 0u || blocks * ZVP_SOUND_F0_BLOCK_BYTES > out_cap) {
        return 0u;
    }
    for (size_t b = 0u; b < blocks; b++) {
        uint8_t *blk = out_f0 + b * ZVP_SOUND_F0_BLOCK_BYTES;
        int32_t max_abs = 0;
        uint8_t shift = 0u;

        for (size_t k = 0u; k < ZVP_SOUND_F0_SAMPLES_PER_BLOCK; k++) {
            size_t idx = b * ZVP_SOUND_F0_SAMPLES_PER_BLOCK + k;
            int32_t v = (idx < count) ? (int32_t)samples[idx] : 0;
            int32_t av = v < 0 ? -v : v;
            if (av > max_abs) {
                max_abs = av;
            }
        }
        while (shift < 7u && (max_abs >> shift) > 127) {
            shift++;
        }
        blk[0] = shift;
        for (size_t k = 0u; k < ZVP_SOUND_F0_SAMPLES_PER_BLOCK; k++) {
            size_t idx = b * ZVP_SOUND_F0_SAMPLES_PER_BLOCK + k;
            int32_t v = (idx < count) ? (int32_t)samples[idx] : 0;
            int32_t q = v >> shift;
            if (q > 127) q = 127;
            if (q < -128) q = -128;
            blk[1u + k] = (uint8_t)(int8_t)q;
        }
    }
    return blocks * ZVP_SOUND_F0_BLOCK_BYTES;
}

int zvp_wav_to_f0(const uint8_t *wav,
                  size_t wav_len,
                  uint8_t *out_f0,
                  size_t out_cap,
                  size_t *out_len)
{
    zvp_wav_format_t fmt;
    if (!out_f0 || !out_len || zvp_wav_parse(wav, wav_len, &fmt) != 0) {
        return -1;
    }

    const uint8_t *pcm = wav + fmt.data_offset;
    uint64_t out_frames64 = ((uint64_t)fmt.frames * ZVP_SOUND_TARGET_RATE + fmt.sample_rate / 2u) /
                            fmt.sample_rate;
    if (out_frames64 == 0u) {
        out_frames64 = 1u;
    }
    if (out_frames64 > (uint64_t)ZVP_SOUND_TARGET_RATE * ZVP_SOUND_MAX_SECONDS) {
        out_frames64 = (uint64_t)ZVP_SOUND_TARGET_RATE * ZVP_SOUND_MAX_SECONDS;
    }
    size_t out_frames = (size_t)out_frames64;
    size_t need_bytes = ((out_frames + ZVP_SOUND_F0_SAMPLES_PER_BLOCK - 1u) /
                         ZVP_SOUND_F0_SAMPLES_PER_BLOCK) * ZVP_SOUND_F0_BLOCK_BYTES;
    if (need_bytes > out_cap) {
        return -2;
    }

    int16_t *mono24 = (int16_t *)malloc(out_frames * sizeof(int16_t));
    if (!mono24) {
        return -3;
    }

    if (fmt.sample_rate == ZVP_SOUND_TARGET_RATE) {
        for (size_t i = 0u; i < out_frames; i++) {
            uint32_t fi = (uint32_t)((i < fmt.frames) ? i : (fmt.frames - 1u));
            int64_t q31 = mono_q31_at(pcm, &fmt, fi);
            mono24[i] = (int16_t)clamp_i16_val((q31 + 32768) >> 16);
        }
    } else {
        /* 使用 Q24 Sinc 滤波重采样至 24kHz（当降采样时缩放截止频率避免混叠） */
        bool down = fmt.sample_rate > ZVP_SOUND_TARGET_RATE;
        uint64_t ratio_q24 = down
            ? (((uint64_t)ZVP_SOUND_TARGET_RATE << 24) / fmt.sample_rate)
            : (1ULL << 24);
        int32_t span = (int32_t)((12ULL << 24) / ratio_q24) + 2;
        if (span > 96) {
            span = 96;
        }
        uint64_t step_q24 = (((uint64_t)fmt.sample_rate << 24) + ZVP_SOUND_TARGET_RATE / 2u) /
                            ZVP_SOUND_TARGET_RATE;

        for (size_t i = 0u; i < out_frames; i++) {
            uint64_t center_q24 = (uint64_t)i * step_q24;
            int64_t center_i = (int64_t)(center_q24 >> 24);
            int64_t start = center_i - span;
            int64_t stop = center_i + span;
            int64_t acc_s = 0;
            int64_t acc_w = 0;

            if (start < 0) start = 0;
            if (stop >= (int64_t)fmt.frames) stop = (int64_t)fmt.frames - 1;

            for (int64_t k = start; k <= stop; k++) {
                uint64_t k_q24 = (uint64_t)k << 24;
                uint64_t d_in = (k_q24 >= center_q24) ? (k_q24 - center_q24) : (center_q24 - k_q24);
                uint64_t d_scaled = (d_in * ratio_q24) >> 24;
                if (d_scaled >= (12ULL << 24)) {
                    continue;
                }
                int32_t w = (int32_t)kernel_at_q24((uint32_t)d_scaled);
                int64_t s = mono_q31_at(pcm, &fmt, (uint32_t)k) >> 16; /* Q15 */
                acc_s += s * (int64_t)w;
                acc_w += (int64_t)w;
            }
            if (acc_w == 0) {
                uint32_t fi = (center_i < (int64_t)fmt.frames)
                                  ? (uint32_t)center_i
                                  : (fmt.frames - 1u);
                mono24[i] = (int16_t)clamp_i16_val(mono_q31_at(pcm, &fmt, fi) >> 16);
            } else {
                mono24[i] = (int16_t)clamp_i16_val(acc_s / acc_w);
            }
        }
    }

    size_t f0_bytes = encode_i16_to_f0(mono24, out_frames, out_f0, out_cap);
    free(mono24);
    if (f0_bytes == 0u) {
        return -2;
    }
    *out_len = f0_bytes;
    return 0;
}

/* ============================================================================
 * 2. 内置合成快门音预设（无需外部 WAV 也可立即体验）
 * ========================================================================== */

/* Q12 三角波/锯齿复合谐波辅助，纯整数运算无需 libm */
static int32_t tri_osc(uint32_t phase_q16)
{
    uint32_t p = phase_q16 & 0xFFFFu;
    int32_t v = (p < 0x8000u) ? (int32_t)(p * 2u) - 32768
                              : (int32_t)((0xFFFFu - p) * 2u) - 32767;
    return v;
}

size_t zvp_sound_build_builtin_f0(uint8_t preset_id, uint8_t *out_f0, size_t out_cap)
{
    if (!out_f0 || out_cap < 2048u) {
        return 0u;
    }
    /* 1674 samples ≈ 69.75 ms @ 24kHz -> 54 个 F0 块 = 1728 字节 */
    enum { N_SAMPLES = 1674 };
    int16_t buf[N_SAMPLES];
    memset(buf, 0, sizeof(buf));

    if (preset_id == 0u) {
        /* 预设 0: 清脆单反双瞬态快门声 (t=0ms 前帘啪 + t=22ms 后帘咔) */
        uint32_t ph1 = 0u, ph2 = 0u;
        for (int i = 0; i < N_SAMPLES; i++) {
            int32_t val = 0;
            if (i < 420) {
                int32_t env = (420 - i) * 22 / 420;
                ph1 += 4915u; /* ~1800 Hz */
                int32_t noise = (int32_t)(((uint32_t)(i * 1103515245u + 12345u) >> 16) & 0x3FFFu) - 8192;
                val += ((tri_osc(ph1) * 3 + noise * 2) * env) / 100;
            }
            if (i >= 520 && i < 1260) {
                int k = i - 520;
                int32_t env = (740 - k) * 28 / 740;
                ph2 += 3400u; /* ~1245 Hz */
                int32_t noise = (int32_t)(((uint32_t)(k * 1664525u + 1013904223u) >> 16) & 0x3FFFu) - 8192;
                val += ((tri_osc(ph2) * 3 + noise) * env) / 100;
            }
            buf[i] = (int16_t)clamp_i16_val(val);
        }
    } else {
        /* 预设 1: 轻快电子快门双音 (t=0ms 3200Hz + t=28ms 4000Hz) */
        uint32_t ph1 = 0u, ph2 = 0u;
        for (int i = 0; i < N_SAMPLES; i++) {
            int32_t val = 0;
            if (i < 550) {
                int32_t env = (i < 40) ? (i * 18 / 40) : ((550 - i) * 18 / 510);
                ph1 += 8738u; /* ~3200 Hz */
                val += (tri_osc(ph1) * env) / 100;
            }
            if (i >= 680 && i < 1420) {
                int k = i - 680;
                int32_t env = (k < 40) ? (k * 20 / 40) : ((740 - k) * 20 / 700);
                ph2 += 10922u; /* ~4000 Hz */
                val += (tri_osc(ph2) * env) / 100;
            }
            buf[i] = (int16_t)clamp_i16_val(val);
        }
    }
    return encode_i16_to_f0(buf, N_SAMPLES, out_f0, out_cap);
}

/* ============================================================================
 * 3. 运行时系统接口与 DSP 槽位动态注入 (ZVP_SOUND_ACTIVE_BIN_PATH)
 * ========================================================================== */

#define ZVP_SYSSND_TABLE_OFF    140u
#define ZVP_SYSSND_ENTRY_STRIDE 160u
#define ZVP_SYSSND_NAME_OFF     8u
#define ZVP_SYSSND_ADF_OFF      10860u /* 140 + 67 * 160 = 10860 (libObj.so 0x311984 / 0x31289a) */
#define ZVP_SYSSND_ADF_SLOTS    20u
#define ZVP_SYSSND_ADF_STRIDE   16u
#define ZVP_SYSSND_MAX_ID       67u

/* 21 个固件默认未使用的 APP None 槽位（鸽巢原理：总槽位 21 > DSP 表容量 20） */
static const uint8_t k_unused_app_slots[21] = {
    0x2du, 0x2eu, 0x2fu,
    0x12u, 0x13u, 0x14u, 0x15u, 0x16u, 0x17u,
    0x19u, 0x1au, 0x1bu,
    0x1du, 0x1eu, 0x1fu, 0x20u, 0x21u, 0x22u, 0x23u, 0x24u, 0x25u
};

static uint8_t *volatile g_sys_snd_data = NULL;
static void    *volatile g_sound_obj_if = NULL;
static bool     g_remap_active = false;
static volatile uint32_t g_sys_snd_logged = 0u;
static volatile uint32_t g_play_diag_count = 0u;

#define ZVP_SYSSND_MEMID_OFF         4u
#define ZVP_SYSSND_RSRCACS_OFF       136u
#define ZVP_SYSSND_PRIO_OFF          140u
#define ZVP_SYSSND_KINDID_OFF        141u
#define ZVP_SYSSND_CONT_OFF          142u
#define ZVP_SYSSND_CONTLEN_OFF       144u
#define ZVP_SYSSND_CONTFIRST_OFF     148u
#define ZVP_SYSSND_MEMTYPE_OFF       152u
#define ZVP_SYSSND_EXT_OFF           156u
#define ZVP_SYSSND_SHUTTER_EN_OFF    11180u /* 11136 + 44 */
#define ZVP_MEMID_APP                0x00070817u
#define ZVP_RSRCACS_AA_RT            2u

static volatile uint32_t g_table_dumped = 0u;
static bool    s_saved_slot_valid[ZVP_SYSSND_MAX_ID];
static uint8_t s_saved_slot_entries[ZVP_SYSSND_MAX_ID][ZVP_SYSSND_ENTRY_STRIDE];
static uint8_t s_saved_slot_sh_en[ZVP_SYSSND_MAX_ID];

static void dump_sound_table_slot(const uint8_t *sys_snd, uint8_t slot, const char *tag)
{
#if !defined(ZVP_HOST_TEST) && !defined(_WIN32)
    if (!sys_snd || slot >= ZVP_SYSSND_MAX_ID) {
        return;
    }
    const uint8_t *e = sys_snd + ZVP_SYSSND_TABLE_OFF +
                       (size_t)slot * ZVP_SYSSND_ENTRY_STRIDE;
    uint32_t sid = 0u, mid = 0u, racs = 0u, clen = 0u, cfirst = 0u, ext = 0u;
    memcpy(&sid, e + 0u, 4u);
    memcpy(&mid, e + ZVP_SYSSND_MEMID_OFF, 4u);
    memcpy(&racs, e + ZVP_SYSSND_RSRCACS_OFF, 4u);
    memcpy(&clen, e + ZVP_SYSSND_CONTLEN_OFF, 4u);
    memcpy(&cfirst, e + ZVP_SYSSND_CONTFIRST_OFF, 4u);
    memcpy(&ext, e + ZVP_SYSSND_EXT_OFF, 4u);
    uint8_t prio = e[ZVP_SYSSND_PRIO_OFF];
    uint8_t kind = e[ZVP_SYSSND_KINDID_OFF];
    uint8_t cont = e[ZVP_SYSSND_CONT_OFF];
    uint8_t mtype = e[ZVP_SYSSND_MEMTYPE_OFF];
    uint8_t sh_en = sys_snd[ZVP_SYSSND_SHUTTER_EN_OFF + slot];
    char name_buf[36];
    memcpy(name_buf, e + ZVP_SYSSND_NAME_OFF, 35u);
    name_buf[35] = '\0';
    sound_logf("SoundTable[%s 0x%02x]: sid=0x%x mem=0x%x acs=%u prio=%u kind=%u cont=%u cl=%u cf=%u mtype=%u ext=%u shEn=%u fn=%s",
               tag, (unsigned)slot, (unsigned)sid, (unsigned)mid, (unsigned)racs,
               (unsigned)prio, (unsigned)kind, (unsigned)cont, (unsigned)clen,
               (unsigned)cfirst, (unsigned)mtype, (unsigned)ext, (unsigned)sh_en, name_buf);
#else
    (void)sys_snd;
    (void)slot;
    (void)tag;
#endif
}

void zvp_sound_on_sys_data(void *sys_snd_data)
{
    if (sys_snd_data && (((uintptr_t)sys_snd_data & 3u) == 0u)) {
        __atomic_store_n(&g_sys_snd_data, (uint8_t *)sys_snd_data, __ATOMIC_RELEASE);
        if (__sync_bool_compare_and_swap(&g_sys_snd_logged, 0u, 1u)) {
            sound_logf("sound 捕获 SoundTable=%p", sys_snd_data);
        }
    }
}

void zvp_sound_on_obj_if(void *obj_if)
{
    if (obj_if) {
        void *prev = __atomic_exchange_n(&g_sound_obj_if, obj_if, __ATOMIC_ACQ_REL);
        if (!prev) {
            sound_logf("sound hook 捕获 ObjSystemSound ObjIf=%p", obj_if);
        }
    }
}

bool zvp_sound_has_active_remap(void)
{
    return __atomic_load_n(&g_remap_active, __ATOMIC_ACQUIRE);
}

const char *zvp_sound_remap_fopen_path(const char *path)
{
    if (!path) {
        return path;
    }
    const size_t pfx_len = sizeof(ZVP_SOUND_SLOT_PREFIX) - 1u;
    if (strncmp(path, ZVP_SOUND_SLOT_PREFIX, pfx_len) == 0) {
        return ZVP_SOUND_ACTIVE_BIN_PATH;
    }
    return path;
}

static void dump_adf_table_summary(const uint8_t *sys_snd, const char *tag)
{
#if !defined(ZVP_HOST_TEST) && !defined(_WIN32)
    if (!sys_snd) {
        return;
    }
    const uintptr_t tbl_start = (uintptr_t)(sys_snd + ZVP_SYSSND_TABLE_OFF);
    const uintptr_t tbl_end = tbl_start + (size_t)ZVP_SYSSND_MAX_ID * ZVP_SYSSND_ENTRY_STRIDE;
    char line[320];
    for (uint32_t half = 0u; half < 2u; half++) {
        size_t pos = 0u;
        uint32_t start_i = half * 10u;
        uint32_t end_i = start_i + 10u;
        pos += (size_t)snprintf(line + pos, sizeof(line) - pos, "Adf[%s %u..%u]:",
                                tag ? tag : "?", (unsigned)start_i, (unsigned)(end_i - 1u));
        for (uint32_t i = start_i; i < end_i && pos + 28u < sizeof(line); i++) {
            const uint8_t *ent = sys_snd + ZVP_SYSSND_ADF_OFF + i * ZVP_SYSSND_ADF_STRIDE;
            uintptr_t info_addr = 0u;
            uint32_t status = 0u;
            memcpy(&info_addr, ent + 0u, sizeof(info_addr));
            memcpy(&status, ent + 8u, sizeof(status));
            uint8_t adf_id = ent[6u];
            uint8_t lru = ent[7u];
            uint8_t fixed = ent[12u];
            int sid = -1;
            if (info_addr >= tbl_start && info_addr + 4u <= tbl_end &&
                ((info_addr - tbl_start) % ZVP_SYSSND_ENTRY_STRIDE) == 0u) {
                sid = (int)(*(const uint32_t *)info_addr);
            }
            pos += (size_t)snprintf(line + pos, sizeof(line) - pos,
                                    " [%02x:s%u:f%u:a%u:id%d]",
                                    (unsigned)adf_id, (unsigned)status,
                                    (unsigned)fixed, (unsigned)lru, sid);
        }
        sound_logf("%s", line);
    }
#else
    (void)sys_snd;
    (void)tag;
#endif
}

static void bind_sound_table_slot(uint8_t *sys_snd, uint8_t slot)
{
    if (!sys_snd || slot >= ZVP_SYSSND_MAX_ID) {
        return;
    }
    if (__sync_bool_compare_and_swap(&g_table_dumped, 0u, 1u)) {
        dump_sound_table_slot(sys_snd, 0x02u, "SHA");
        dump_sound_table_slot(sys_snd, 0x04u, "AF");
        dump_sound_table_slot(sys_snd, 0x08u, "SEL");
        dump_sound_table_slot(sys_snd, slot, "PRE");
    }
    uint8_t *entry = sys_snd + ZVP_SYSSND_TABLE_OFF +
                     (size_t)slot * ZVP_SYSSND_ENTRY_STRIDE;
    uint32_t entry_id = *(const uint32_t *)entry;
    if (entry_id != (uint32_t)slot) {
        return;
    }
    if (!s_saved_slot_valid[slot]) {
        memcpy(s_saved_slot_entries[slot], entry, ZVP_SYSSND_ENTRY_STRIDE);
#if !defined(ZVP_HOST_TEST) && !defined(_WIN32)
        s_saved_slot_sh_en[slot] = sys_snd[ZVP_SYSSND_SHUTTER_EN_OFF + slot];
#else
        s_saved_slot_sh_en[slot] = 0u;
#endif
        s_saved_slot_valid[slot] = true;
    }
    /* 确保槽位内存池与资源类型保持为 APP (0x00070817) + AA_RT (2) + 高优先级 (6) */
    uint32_t *mem_id = (uint32_t *)(entry + ZVP_SYSSND_MEMID_OFF);
    uint32_t *rsrc_acs = (uint32_t *)(entry + ZVP_SYSSND_RSRCACS_OFF);
    uint8_t *mem_type = (uint8_t *)(entry + ZVP_SYSSND_MEMTYPE_OFF);
    if (*mem_id == 0u) {
        *mem_id = ZVP_MEMID_APP;
    }
    if (*rsrc_acs != ZVP_RSRCACS_AA_RT) {
        *rsrc_acs = ZVP_RSRCACS_AA_RT;
    }
    if (*mem_type == 0u) {
        *mem_type = 1u;
    }
    if (entry[ZVP_SYSSND_PRIO_OFF] < 6u) {
        entry[ZVP_SYSSND_PRIO_OFF] = 6u;
    }
#if !defined(ZVP_HOST_TEST) && !defined(_WIN32)
    sys_snd[ZVP_SYSSND_SHUTTER_EN_OFF + slot] = 1u;
#endif
    char *fname = (char *)(entry + ZVP_SYSSND_NAME_OFF);
    snprintf(fname, 64u, "zvp_slot_%02x.bin", (unsigned)slot);
    if (__atomic_load_n(&g_play_diag_count, __ATOMIC_RELAXED) < 64u) {
        dump_sound_table_slot(sys_snd, slot, "BIND");
    }
}

static int find_adf_slot_idx(const uint8_t *sys_snd,
                             uint8_t sound_id,
                             uint32_t *out_status,
                             uint8_t *out_adf_id)
{
    if (!sys_snd) {
        return -1;
    }
    const uintptr_t tbl_start = (uintptr_t)(sys_snd + ZVP_SYSSND_TABLE_OFF);
    const uintptr_t tbl_end = tbl_start + (size_t)ZVP_SYSSND_MAX_ID * ZVP_SYSSND_ENTRY_STRIDE;

    for (uint32_t i = 0u; i < ZVP_SYSSND_ADF_SLOTS; i++) {
        const uint8_t *ent = sys_snd + ZVP_SYSSND_ADF_OFF + i * ZVP_SYSSND_ADF_STRIDE;
        uintptr_t info_addr = 0u;
        uint32_t status = 0u;
        memcpy(&info_addr, ent + 0u, sizeof(info_addr));
        memcpy(&status, ent + 8u, sizeof(status));
        if (status != 0u &&
            info_addr >= tbl_start &&
            info_addr + sizeof(uint32_t) <= tbl_end &&
            ((info_addr - tbl_start) % ZVP_SYSSND_ENTRY_STRIDE) == 0u) {
            /* SoundTable 首字段即 soundId (32位)，仅在地址严格落入 SoundTable 条目首部时解引用 */
            uint32_t sid = *(const uint32_t *)info_addr;
            if (sid == (uint32_t)sound_id) {
                if (out_status) *out_status = status;
                if (out_adf_id) *out_adf_id = ent[6u];
                return (int)i;
            }
        }
    }
    return -1;
}

static bool is_slot_in_adf_cache(const uint8_t *sys_snd, uint8_t sound_id)
{
    return find_adf_slot_idx(sys_snd, sound_id, NULL, NULL) >= 0;
}

static bool is_slot_bound_by_us(const uint8_t *sys_snd, uint8_t slot)
{
    if (!sys_snd || slot >= ZVP_SYSSND_MAX_ID) {
        return false;
    }
    const uint8_t *entry = sys_snd + ZVP_SYSSND_TABLE_OFF +
                           (size_t)slot * ZVP_SYSSND_ENTRY_STRIDE;
    char expected[32];
    snprintf(expected, sizeof(expected), "zvp_slot_%02x.bin", (unsigned)slot);
    return strncmp((const char *)(entry + ZVP_SYSSND_NAME_OFF), expected, 32u) == 0;
}

static bool cache_matches_item(const zvp_sound_slot_cache_t *c, const zvp_sound_item_t *it)
{
    if (!c || !c->occupied || !it) {
        return false;
    }
    if (c->is_builtin != it->is_builtin) {
        return false;
    }
    if (c->is_builtin) {
        return c->builtin_id == it->builtin_id;
    }
    return strcmp(c->path, it->path) == 0;
}

static bool slot_in_mgr_cache(const zvp_sound_mgr_t *mgr, uint8_t slot)
{
    for (uint32_t i = 0u; i < ZVP_SOUND_CACHE_SLOTS; i++) {
        if (mgr->slot_cache[i].occupied && mgr->slot_cache[i].slot == slot) {
            return true;
        }
    }
    return false;
}

static void unregist_and_clear_slot(uint8_t *sys_snd, uint8_t slot)
{
    if (!sys_snd) {
        return;
    }
    uint32_t st = 0u;
    uint8_t adf_id = 0u;
    int idx = find_adf_slot_idx(sys_snd, slot, &st, &adf_id);
    if (idx < 0) {
        return;
    }
#if !defined(ZVP_HOST_TEST) && !defined(_WIN32)
    if (adf_id >= 1u && adf_id <= ZVP_SYSSND_ADF_SLOTS) {
        (void)zvp_sound_hook_unregist_adf(adf_id);
        usleep(15000);
    }
#else
    (void)st;
    (void)adf_id;
#endif
    uint8_t *ent = sys_snd + ZVP_SYSSND_ADF_OFF + (uint32_t)idx * ZVP_SYSSND_ADF_STRIDE;
    uintptr_t zero_ptr = 0u;
    uint32_t zero_u32 = 0u;
    memcpy(ent + 0u, &zero_ptr, sizeof(zero_ptr));
    ent[4u] = 0u;
    ent[5u] = 0u;
    ent[7u] = 0u;
    memcpy(ent + 8u, &zero_u32, sizeof(zero_u32));
    ent[12u] = 0u;
    sound_logf("sound 卸载旧槽位完成: slot=0x%02x adf=0x%02x", (unsigned)slot, (unsigned)adf_id);
}

static uint8_t pick_and_bind_slot_for_item(zvp_sound_mgr_t *mgr,
                                           const zvp_sound_item_t *it,
                                           uint32_t f0_bytes)
{
    uint32_t n = (uint32_t)(sizeof(k_unused_app_slots) / sizeof(k_unused_app_slots[0]));
    uint8_t *sys_snd = __atomic_load_n(&g_sys_snd_data, __ATOMIC_ACQUIRE);
    int target_idx = -1;
    uint8_t chosen = k_unused_app_slots[mgr->slot_cursor % n];

    /* 0. 若某个缓存槽位在硬件 adfTable 中被非本插件音色占用（例如初始化前预占冲突），作废该缓存项 */
    if (sys_snd) {
        for (uint32_t i = 0u; i < ZVP_SOUND_CACHE_SLOTS; i++) {
            if (mgr->slot_cache[i].occupied &&
                is_slot_in_adf_cache(sys_snd, mgr->slot_cache[i].slot) &&
                !is_slot_bound_by_us(sys_snd, mgr->slot_cache[i].slot)) {
                mgr->slot_cache[i].occupied = false;
            }
        }
    }

    /* 1. 优先复用该音效已分配且尚未切走的缓存槽位（含尚未进 adfTable 的情况） */
    if (it) {
        for (uint32_t i = 0u; i < ZVP_SOUND_CACHE_SLOTS; i++) {
            if (cache_matches_item(&mgr->slot_cache[i], it)) {
                target_idx = (int)i;
                chosen = mgr->slot_cache[i].slot;
                break;
            }
        }
    }

    /* 2. 若未匹配到已有条目，优先回收尚未实际加载进 adfTable 的闲置绑定槽位 */
    if (target_idx < 0 && sys_snd) {
        for (uint32_t i = 0u; i < ZVP_SOUND_CACHE_SLOTS; i++) {
            if (mgr->slot_cache[i].occupied &&
                !is_slot_in_adf_cache(sys_snd, mgr->slot_cache[i].slot)) {
                target_idx = (int)i;
                chosen = mgr->slot_cache[i].slot;
                break;
            }
        }
    }

    /* 3. 若仍有未占用缓存位 (< 4)，从空闲保留槽位中选出未在 adfTable 也未在缓存中的新槽位 */
    if (target_idx < 0) {
        for (uint32_t i = 0u; i < ZVP_SOUND_CACHE_SLOTS; i++) {
            if (!mgr->slot_cache[i].occupied) {
                target_idx = (int)i;
                break;
            }
        }
        if (target_idx >= 0) {
            bool found_free = false;
            for (uint32_t step = 0u; step < n; step++) {
                uint8_t cand = k_unused_app_slots[(mgr->slot_cursor + step) % n];
                if (!slot_in_mgr_cache(mgr, cand) &&
                    (!sys_snd || !is_slot_in_adf_cache(sys_snd, cand))) {
                    chosen = cand;
                    mgr->slot_cursor = (mgr->slot_cursor + step + 1u) % n;
                    found_free = true;
                    break;
                }
            }
            if (!found_free) {
                mgr->slot_cursor = (mgr->slot_cursor + 1u) % n;
            }
        }
    }

    /* 4. 4 个活跃缓存位全部在 adfTable 中已占用（>4 个不同音频）：LRU 淘汰并主动卸载最旧槽位 */
    if (target_idx < 0) {
        uint32_t victim_i = 0u;
        uint32_t best_seq = UINT32_MAX;
        for (uint32_t i = 0u; i < ZVP_SOUND_CACHE_SLOTS; i++) {
            uint32_t st = 0u;
            if (sys_snd) {
                (void)find_adf_slot_idx(sys_snd, mgr->slot_cache[i].slot, &st, NULL);
            }
            uint32_t penalty = (st == 6u) ? 0x80000000u : 0u;
            uint32_t score = mgr->slot_cache[i].last_seq;
            if (penalty == 0u && score <= best_seq) {
                best_seq = score;
                victim_i = i;
            } else if (best_seq == UINT32_MAX) {
                victim_i = i;
            }
        }
        target_idx = (int)victim_i;
        chosen = mgr->slot_cache[victim_i].slot;
        unregist_and_clear_slot(sys_snd, chosen);
        mgr->evict_count++;
    }

    if (sys_snd) {
        if (__atomic_load_n(&g_play_diag_count, __ATOMIC_RELAXED) < 64u) {
            dump_adf_table_summary(sys_snd, "PICK");
        }
        bind_sound_table_slot(sys_snd, chosen);
    }

    if (target_idx >= 0 && it) {
        zvp_sound_slot_cache_t *c = &mgr->slot_cache[target_idx];
        c->occupied = true;
        c->slot = chosen;
        c->is_builtin = it->is_builtin;
        c->builtin_id = it->builtin_id;
        snprintf(c->path, sizeof(c->path), "%s", it->path);
        c->f0_bytes = f0_bytes;
        c->last_seq = ++mgr->cache_seq;
    }

    __atomic_store_n(&g_remap_active, true, __ATOMIC_RELEASE);
    mgr->active_slot = chosen;
    return chosen;
}

#ifndef _WIN32
typedef void (*fn_objmsg_ctor_t)(void *msg, uint32_t cat_id, uint32_t msg_id);
typedef void (*fn_objmsg_dtor_t)(void *msg);
typedef void (*fn_objmsg_addp_t)(void *msg, uint32_t param_id, const void *param);
typedef void (*fn_paramu32_ctor_t)(void *param, uint32_t val);
typedef void (*fn_paramu32_dtor_t)(void *param);
typedef int  (*fn_issue_async_t)(void *obj_if, void *msg);
typedef int  (*fn_objif_get_inst_t)(const char *name, int index, void **out_obj_if);
#endif

static bool hw_play_slot(uint8_t sound_id, int32_t *out_err)
{
#ifdef _WIN32
    (void)sound_id;
    (void)out_err;
    return true;
#else
    void *obj_if = __atomic_load_n(&g_sound_obj_if, __ATOMIC_ACQUIRE);
    if (!obj_if) {
        /* 通过 MWF::ObjIf::GetInstance("ObjSystemSound", 0, &inst) 直接取得对象接口 */
        fn_objif_get_inst_t get_inst = (fn_objif_get_inst_t)dlsym(
            RTLD_DEFAULT, "_ZN3MWF5ObjIf11GetInstanceEPKciPPv");
        if (!get_inst) {
            sound_logf("sound 错误: dlsym 缺失 MWF::ObjIf::GetInstance");
            if (out_err) *out_err = -8;
            return false;
        }
        void *inst = NULL;
        int grc = get_inst("ObjSystemSound", 0, &inst);
        if (grc != 0 || !inst) {
            sound_logf("sound GetInstance(\"ObjSystemSound\", 0) 失败 rc=%d inst=%p", grc, inst);
            if (out_err) *out_err = -8;
            return false;
        }
        __atomic_store_n(&g_sound_obj_if, inst, __ATOMIC_RELEASE);
        obj_if = inst;
        sound_logf("sound GetInstance(\"ObjSystemSound\", 0) 成功 inst=%p", obj_if);
    }

    static fn_objmsg_ctor_t   s_msg_ctor   = NULL;
    static fn_objmsg_dtor_t   s_msg_dtor   = NULL;
    static fn_objmsg_addp_t   s_msg_addp   = NULL;
    static fn_paramu32_ctor_t s_prm_ctor   = NULL;
    static fn_paramu32_dtor_t s_prm_dtor   = NULL;
    static fn_issue_async_t   s_issue_fn   = NULL;
    static volatile int       s_resolved   = 0;

    if (__atomic_load_n(&s_resolved, __ATOMIC_ACQUIRE) == 0) {
        s_msg_ctor = (fn_objmsg_ctor_t)dlsym(RTLD_DEFAULT, "_ZN3MWF6ObjMsgC1Ejj");
        s_msg_dtor = (fn_objmsg_dtor_t)dlsym(RTLD_DEFAULT, "_ZN3MWF6ObjMsgD1Ev");
        s_msg_addp = (fn_objmsg_addp_t)dlsym(RTLD_DEFAULT, "_ZN3MWF6ObjMsg8AddParamEjRKNS_5ParamE");
        s_prm_ctor = (fn_paramu32_ctor_t)dlsym(RTLD_DEFAULT, "_ZN3MWF17ParamStd_TemplateIjLNS_5Param4TypeE8EEC1Ej");
        s_prm_dtor = (fn_paramu32_dtor_t)dlsym(RTLD_DEFAULT, "_ZN3MWF17ParamStd_TemplateIjLNS_5Param4TypeE8EED1Ev");
        s_issue_fn = (fn_issue_async_t)dlsym(RTLD_NEXT, "_ZN3MWF5ObjIf17IssueCommandAsyncEPvPNS_6ObjMsgE");
        if (!s_issue_fn) {
            s_issue_fn = (fn_issue_async_t)dlsym(RTLD_DEFAULT, "_ZN3MWF5ObjIf17IssueCommandAsyncEPvPNS_6ObjMsgE");
        }
        __atomic_store_n(&s_resolved, 1, __ATOMIC_RELEASE);
    }

    if (!s_msg_ctor || !s_msg_dtor || !s_msg_addp || !s_prm_ctor || !s_prm_dtor || !s_issue_fn) {
        sound_logf("sound 符号缺失: ctor=%p dtor=%p addp=%p pctor=%p pdtor=%p issue=%p",
                   (void *)s_msg_ctor, (void *)s_msg_dtor, (void *)s_msg_addp,
                   (void *)s_prm_ctor, (void *)s_prm_dtor, (void *)s_issue_fn);
        if (out_err) *out_err = -9;
        return false;
    }

    uint64_t msg_buf[16];
    uint64_t p0[8], p1[8], p2[8];
    memset(msg_buf, 0, sizeof(msg_buf));
    memset(p0, 0, sizeof(p0));
    memset(p1, 0, sizeof(p1));
    memset(p2, 0, sizeof(p2));

    s_msg_ctor(msg_buf, 0x3410u, 0x1120u);
    s_prm_ctor(p0, (uint32_t)sound_id);
    s_prm_ctor(p1, 1u);
    s_prm_ctor(p2, 0u);

    s_msg_addp(msg_buf, 0x1000u, p0);
    s_msg_addp(msg_buf, 0x1001u, p1);
    s_msg_addp(msg_buf, 0x1002u, p2);

    int rc = s_issue_fn(obj_if, msg_buf);

    s_prm_dtor(p2);
    s_prm_dtor(p1);
    s_prm_dtor(p0);
    s_msg_dtor(msg_buf);

    uint32_t dcnt = __atomic_fetch_add(&g_play_diag_count, 1u, __ATOMIC_RELAXED);
    if (rc != 0 || dcnt < 64u) {
        sound_logf("sound hw_play #%u: slot=0x%02x obj_if=%p sys_snd=%p rc=%d",
                   (unsigned)(dcnt + 1u), (unsigned)sound_id, obj_if,
                   (void *)__atomic_load_n(&g_sys_snd_data, __ATOMIC_ACQUIRE), rc);
    }
    if (rc != 0 && out_err) {
        *out_err = -10;
    }
    return rc == 0;
#endif
}

/* ============================================================================
 * 4. SD 卡音频扫描、选择与转换管理
 * ========================================================================== */

static bool ends_with_wav_ci(const char *name)
{
    if (!name) return false;
    size_t n = strlen(name);
    if (n < 5u || name[0] == '.' || name[0] == ' ') return false;
    if (name[0] == '.' && name[1] == '_') return false;
    return name[n - 4u] == '.' &&
           (name[n - 3u] == 'w' || name[n - 3u] == 'W') &&
           (name[n - 2u] == 'a' || name[n - 2u] == 'A') &&
           (name[n - 1u] == 'v' || name[n - 1u] == 'V');
}

static bool has_item_name(const zvp_sound_mgr_t *mgr, const char *name)
{
    for (uint32_t i = 0u; i < mgr->count; i++) {
        if (strcmp(mgr->items[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

static void add_item(zvp_sound_mgr_t *mgr,
                     const char *name,
                     const char *path,
                     bool is_builtin,
                     uint8_t builtin_id)
{
    if (!mgr || mgr->count >= ZVP_SOUND_MAX_ITEMS || !name || !name[0]) {
        return;
    }
    if (has_item_name(mgr, name)) {
        return;
    }
    zvp_sound_item_t *it = &mgr->items[mgr->count++];
    memset(it, 0, sizeof(*it));
    snprintf(it->name, sizeof(it->name), "%s", name);
    if (path) {
        snprintf(it->path, sizeof(it->path), "%s", path);
    }
    it->is_builtin = is_builtin;
    it->builtin_id = builtin_id;
}

#if !defined(_WIN32) && !defined(ZVP_HOST_TEST)
typedef uint32_t (*zvp_fn_fsys_mounts_t)(void);
typedef int (*zvp_fn_fsys_find_first_t)(const char *pattern, uint32_t attr_mask, void *find_info);
typedef int (*zvp_fn_fsys_find_next_t)(void *find_info);
typedef int (*zvp_fn_fsys_open_t)(const char *path, int *out_handle, int mode);
typedef int (*zvp_fn_fsys_read_t)(int handle, void *buf, uint32_t size);
typedef int (*zvp_fn_fsys_close_t)(int handle);

static zvp_fn_fsys_mounts_t     g_fsys_mounts     = NULL;
static zvp_fn_fsys_find_first_t g_fsys_find_first = NULL;
static zvp_fn_fsys_find_next_t  g_fsys_find_next  = NULL;
static zvp_fn_fsys_open_t       g_fsys_open       = NULL;
static zvp_fn_fsys_read_t       g_fsys_read       = NULL;
static zvp_fn_fsys_close_t      g_fsys_close      = NULL;
static volatile int             g_fsys_resolved   = 0;

static void ensure_fsys_syms(void)
{
    if (__atomic_load_n(&g_fsys_resolved, __ATOMIC_ACQUIRE) != 0) {
        return;
    }
    g_fsys_mounts     = (zvp_fn_fsys_mounts_t)dlsym(RTLD_DEFAULT, "Fsys_GetMountDrives");
    g_fsys_find_first = (zvp_fn_fsys_find_first_t)dlsym(RTLD_DEFAULT, "Fsys_FindFirst");
    g_fsys_find_next  = (zvp_fn_fsys_find_next_t)dlsym(RTLD_DEFAULT, "Fsys_FindNext");
    g_fsys_open       = (zvp_fn_fsys_open_t)dlsym(RTLD_DEFAULT, "Fsys_Open");
    g_fsys_read       = (zvp_fn_fsys_read_t)dlsym(RTLD_DEFAULT, "Fsys_Read");
    g_fsys_close      = (zvp_fn_fsys_close_t)dlsym(RTLD_DEFAULT, "Fsys_Close");
    __atomic_store_n(&g_fsys_resolved, 1, __ATOMIC_RELEASE);
}

static volatile uint32_t g_fsys_probe_count = 0u;

static void probe_fsys_path(const char *pattern)
{
    if (!g_fsys_find_first || !g_fsys_find_next) {
        return;
    }
    uint8_t *fi = (uint8_t *)calloc(1u, 2088u);
    if (!fi) {
        return;
    }
    int rc = g_fsys_find_first(pattern, 0x80u, fi);
    if (rc != 0) {
        sound_logf("sound Fsys 探测 %s -> rc=0x%x", pattern, (unsigned)rc);
        free(fi);
        return;
    }
    char list_buf[220];
    size_t pos = 0u;
    uint32_t cnt = 0u;
    do {
        const char *fname = NULL;
        memcpy(&fname, fi + 0u, sizeof(fname));
        uint8_t attr = fi[38u];
        if (fname && fname[0] && cnt < 8u) {
            int n = snprintf(list_buf + pos, sizeof(list_buf) - pos,
                             "%s%s(0x%02x)", (cnt > 0u) ? "," : "", fname, (unsigned)attr);
            if (n > 0 && (size_t)n < sizeof(list_buf) - pos) {
                pos += (size_t)n;
            }
        }
        cnt++;
    } while (cnt < 32u && g_fsys_find_next(fi) == 0);
    list_buf[pos] = '\0';
    free(fi);
    sound_logf("sound Fsys 探测 %s -> 命中 %u 项: %s", pattern, (unsigned)cnt, list_buf);
}

static bool is_fsys_drive_path(const char *p)
{
    return p && (p[0] >= 'A' && p[0] <= 'Z') && p[1] == ':' && p[2] == '/';
}

static void probe_fsys_sd_layout(void)
{
    ensure_fsys_syms();
    if (!g_fsys_mounts || !g_fsys_find_first) {
        return;
    }
    uint32_t drives = g_fsys_mounts();
    sound_logf("sound Fsys 深度探测 mounts=0x%x", (unsigned)drives);
    for (uint32_t d = 0u; d < 26u; d++) {
        if ((drives & (1u << d)) == 0u) {
            continue;
        }
        char p[96];
        char drv = (char)('A' + d);
        snprintf(p, sizeof(p), "%c:/*.*", drv);
        probe_fsys_path(p);
        snprintf(p, sizeof(p), "%c:/PRIVATE/*.*", drv);
        probe_fsys_path(p);
        snprintf(p, sizeof(p), "%c:/PRIVATE/M4ROOT/*.*", drv);
        probe_fsys_path(p);
        snprintf(p, sizeof(p), "%c:/PRIVATE/M4ROOT/GENERAL/*.*", drv);
        probe_fsys_path(p);
        snprintf(p, sizeof(p), "%c:/PRIVATE/M4ROOT/GENERAL/SOUNDS/*.*", drv);
        probe_fsys_path(p);
    }
}

static void scan_fsys_dir(zvp_sound_mgr_t *mgr, const char *dir_path)
{
    if (!mgr || !is_fsys_drive_path(dir_path) ||
        mgr->count >= ZVP_SOUND_MAX_ITEMS) {
        return;
    }
    ensure_fsys_syms();
    if (!g_fsys_mounts || !g_fsys_find_first || !g_fsys_find_next) {
        sound_logf("sound Fsys 符号缺失 mounts=%p ff=%p fn=%p",
                   (void *)g_fsys_mounts, (void *)g_fsys_find_first, (void *)g_fsys_find_next);
        return;
    }
    uint32_t drives = g_fsys_mounts();
    uint32_t bit = 1u << (uint32_t)(dir_path[0] - 'A');
    if ((drives & bit) == 0u) {
        sound_logf("sound Fsys %c:/ 未挂载 drives=0x%x", dir_path[0], (unsigned)drives);
        return;
    }

    static const char *const k_globs[] = { "*.*", "*.WAV", "*.wav", "*" };
    uint8_t *find_info = (uint8_t *)calloc(1u, 2088u);
    if (!find_info) {
        return;
    }

    int rc = -1;
    char matched_pattern[ZVP_SOUND_PATH_LEN];
    matched_pattern[0] = '\0';

    for (size_t g = 0u; g < sizeof(k_globs) / sizeof(k_globs[0]); g++) {
        char pattern[ZVP_SOUND_PATH_LEN];
        int pn = snprintf(pattern, sizeof(pattern), "%s/%s", dir_path, k_globs[g]);
        if (pn <= 0 || (size_t)pn >= sizeof(pattern)) {
            continue;
        }
        memset(find_info, 0, 2088u);
        rc = g_fsys_find_first(pattern, 0x80u, find_info);
        if (rc == 0) {
            snprintf(matched_pattern, sizeof(matched_pattern), "%s", pattern);
            break;
        }
    }

    if (rc != 0) {
        sound_logf("sound Fsys FindFirst(%s/{*.*,*.WAV,*}) rc=0x%x", dir_path, (unsigned)rc);
        free(find_info);
        return;
    }

    uint32_t added = 0u;
    do {
        const char *fname = NULL;
        memcpy(&fname, find_info + 0u, sizeof(fname));
        uint8_t attr = find_info[38u];
        if (fname && fname[0] && (attr & 0x10u) == 0u && ends_with_wav_ci(fname)) {
            char full[ZVP_SOUND_PATH_LEN];
            int fn = snprintf(full, sizeof(full), "%s/%s", dir_path, fname);
            if (fn > 0 && (size_t)fn < sizeof(full)) {
                uint32_t prev_cnt = mgr->count;
                add_item(mgr, fname, full, false, 0u);
                if (mgr->count > prev_cnt) {
                    added++;
                    sound_logf("sound Fsys 发现 WAV: %s (attr=0x%02x pat=%s)",
                               full, (unsigned)attr, matched_pattern);
                }
            }
        }
    } while (mgr->count < ZVP_SOUND_MAX_ITEMS && g_fsys_find_next(find_info) == 0);

    free(find_info);
    sound_logf("sound Fsys 扫描 %s (%s) -> 新增 %u 个 WAV", dir_path, matched_pattern, (unsigned)added);
}

static uint8_t *read_fsys_wav_alloc(const char *path, size_t *out_size)
{
    ensure_fsys_syms();
    if (!g_fsys_open || !g_fsys_read || !g_fsys_close) {
        return NULL;
    }
    int handle = -1;
    int rc = g_fsys_open(path, &handle, 0);
    if (rc != 0 || handle < 0) {
        sound_logf("sound Fsys_Open(%s) 失败 rc=0x%x h=%d", path, (unsigned)rc, handle);
        return NULL;
    }
    size_t cap = 65536u;
    size_t total = 0u;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) {
        g_fsys_close(handle);
        return NULL;
    }
    while (total < ZVP_SOUND_MAX_WAV_BYTES) {
        if (cap - total < 16384u) {
            size_t next_cap = cap * 2u;
            if (next_cap > ZVP_SOUND_MAX_WAV_BYTES + 16384u) {
                next_cap = ZVP_SOUND_MAX_WAV_BYTES + 16384u;
            }
            if (next_cap <= cap) {
                break;
            }
            uint8_t *nbuf = (uint8_t *)realloc(buf, next_cap);
            if (!nbuf) {
                free(buf);
                g_fsys_close(handle);
                return NULL;
            }
            buf = nbuf;
            cap = next_cap;
        }
        uint32_t want = (uint32_t)(ZVP_SOUND_MAX_WAV_BYTES + 1u - total);
        if (want > 16384u) {
            want = 16384u;
        }
        int rd = g_fsys_read(handle, buf + total, want);
        if (rd < 0) {
            sound_logf("sound Fsys_Read(%s) 错误 rc=0x%x total=%u", path, (unsigned)rd, (unsigned)total);
            free(buf);
            g_fsys_close(handle);
            return NULL;
        }
        if (rd == 0) {
            break;
        }
        total += (size_t)rd;
    }
    g_fsys_close(handle);
    if (total <= 12u || total > ZVP_SOUND_MAX_WAV_BYTES) {
        sound_logf("sound Fsys 读取大小超限/无效: %s total=%u", path, (unsigned)total);
        free(buf);
        return NULL;
    }
    *out_size = total;
    return buf;
}
#endif /* !_WIN32 && !ZVP_HOST_TEST */

static void scan_dir(zvp_sound_mgr_t *mgr, const char *dir_path)
{
    if (!mgr || !dir_path || !dir_path[0] || mgr->count >= ZVP_SOUND_MAX_ITEMS) {
        return;
    }
#if !defined(_WIN32) && !defined(ZVP_HOST_TEST)
    if (is_fsys_drive_path(dir_path)) {
        scan_fsys_dir(mgr, dir_path);
        return;
    }
#endif
#ifdef _WIN32
    char pattern[300];
    WIN32_FIND_DATAA fd;
    snprintf(pattern, sizeof(pattern), "%s\\*", dir_path);
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        if (ends_with_wav_ci(fd.cFileName)) {
            char full[ZVP_SOUND_PATH_LEN];
            snprintf(full, sizeof(full), "%s/%s", dir_path, fd.cFileName);
            add_item(mgr, fd.cFileName, full, false, 0u);
        }
    } while (mgr->count < ZVP_SOUND_MAX_ITEMS && FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir_path);
    if (!d) {
        return;
    }
    struct dirent *ent;
    while (mgr->count < ZVP_SOUND_MAX_ITEMS && (ent = readdir(d)) != NULL) {
        if (ends_with_wav_ci(ent->d_name)) {
            char full[ZVP_SOUND_PATH_LEN];
            int n = snprintf(full, sizeof(full), "%s/%s", dir_path, ent->d_name);
            if (n > 0 && (size_t)n < sizeof(full)) {
                add_item(mgr, ent->d_name, full, false, 0u);
            }
        }
    }
    closedir(d);
#endif
}

static bool write_bin_file(const char *path, const uint8_t *data, size_t len)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return false;
    }
    size_t written = fwrite(data, 1u, len, fp);
    fclose(fp);
    return written == len;
}

static uint8_t *read_wav_file_alloc(const char *path, size_t *out_size)
{
#if !defined(_WIN32) && !defined(ZVP_HOST_TEST)
    if (is_fsys_drive_path(path)) {
        return read_fsys_wav_alloc(path, out_size);
    }
#endif
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long sz = ftell(fp);
    if (sz <= 12L || (size_t)sz > ZVP_SOUND_MAX_WAV_BYTES) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    size_t rd = fread(buf, 1u, (size_t)sz, fp);
    fclose(fp);
    if (rd != (size_t)sz) {
        free(buf);
        return NULL;
    }
    *out_size = (size_t)sz;
    return buf;
}

void zvp_sound_init(zvp_sound_mgr_t *mgr)
{
    if (!mgr) {
        return;
    }
    memset(mgr, 0, sizeof(*mgr));
    snprintf(mgr->active_bin_path, sizeof(mgr->active_bin_path), "%s", ZVP_SOUND_ACTIVE_BIN_PATH);
    mgr->active_slot = 0x2du;
    zvp_sound_scan(mgr);
}

void zvp_sound_scan(zvp_sound_mgr_t *mgr)
{
    if (!mgr) {
        return;
    }
    char prev_name[ZVP_SOUND_NAME_LEN];
    snprintf(prev_name, sizeof(prev_name), "%s", mgr->selected_name);

    mgr->count = 0u;
    if (mgr->override_dir[0]) {
        scan_dir(mgr, mgr->override_dir);
    } else {
#if !defined(_WIN32) && !defined(ZVP_HOST_TEST)
        ensure_fsys_syms();
        uint32_t drives = g_fsys_mounts ? g_fsys_mounts() : 0x3u;
        /* 优先扫 B:..Z:（ZV-E10 真机中 A: 常为虚拟 INFO/LICENSE 盘，B: 为 SD 卡槽），再扫 A: */
        static const char *const k_sub_dirs[] = {
            "PRIVATE/M4ROOT/GENERAL/SOUNDS",
            "PRIVATE/SONY/PRO/SOUNDS",
            "SOUNDS"
        };
        for (uint32_t pass = 0u; pass < 2u; pass++) {
            uint32_t start = (pass == 0u) ? 1u : 0u;
            uint32_t end = (pass == 0u) ? 26u : 1u;
            for (uint32_t d = start; d < end; d++) {
                if ((drives & (1u << d)) == 0u) {
                    continue;
                }
                for (size_t s = 0u; s < sizeof(k_sub_dirs) / sizeof(k_sub_dirs[0]); s++) {
                    char dir_buf[ZVP_SOUND_PATH_LEN];
                    snprintf(dir_buf, sizeof(dir_buf), "%c:/%s", (char)('A' + d), k_sub_dirs[s]);
                    scan_dir(mgr, dir_buf);
                }
            }
        }
#else
        scan_dir(mgr, "A:/PRIVATE/M4ROOT/GENERAL/SOUNDS");
        scan_dir(mgr, "A:/PRIVATE/SONY/PRO/SOUNDS");
        scan_dir(mgr, "A:/SOUNDS");
#endif
        scan_dir(mgr, "/mnt/sd/PRIVATE/M4ROOT/GENERAL/SOUNDS");
        scan_dir(mgr, "/mnt/sd/PRIVATE/SONY/PRO/SOUNDS");
        scan_dir(mgr, "/tmp/zvp_sounds");
    }

    uint32_t sd_count = mgr->count;
    add_item(mgr, "内置: 清脆快门", "", true, 0u);
    add_item(mgr, "内置: 电子提示", "", true, 1u);

    if (sd_count > 0u) {
        snprintf(mgr->last_source_label, sizeof(mgr->last_source_label),
                 "SD卡 %u 个 + 内置 %u 个", (unsigned)sd_count, (unsigned)(mgr->count - sd_count));
    } else {
        snprintf(mgr->last_source_label, sizeof(mgr->last_source_label),
                 "仅内置预设 (未发现SD WAV)");
#if !defined(_WIN32) && !defined(ZVP_HOST_TEST)
        if (!mgr->override_dir[0] &&
            __atomic_fetch_add(&g_fsys_probe_count, 1u, __ATOMIC_RELAXED) < 3u) {
            probe_fsys_sd_layout();
        }
#endif
    }

    mgr->scanned = true;
    mgr->prepared = false;

    if (prev_name[0] && zvp_sound_select_by_name(mgr, prev_name)) {
        return;
    }
    mgr->selected_idx = 0u;
    if (mgr->count > 0u) {
        snprintf(mgr->selected_name, sizeof(mgr->selected_name), "%s", mgr->items[0].name);
    }
}

bool zvp_sound_prepare_current(zvp_sound_mgr_t *mgr)
{
    if (!mgr) {
        return false;
    }
    if (!mgr->scanned) {
        zvp_sound_scan(mgr);
    }
    if (mgr->count == 0u) {
        mgr->last_error = -1;
        return false;
    }
    if (mgr->selected_idx >= mgr->count) {
        mgr->selected_idx = 0u;
    }

    const zvp_sound_item_t *it = &mgr->items[mgr->selected_idx];
    uint8_t *sys_snd = __atomic_load_n(&g_sys_snd_data, __ATOMIC_ACQUIRE);

    /* 若该音效已在硬件 adfTable 缓存且 SoundTable 绑定完整，直接命中复用，避免重复注册与超额阻塞 */
    if (sys_snd) {
        for (uint32_t i = 0u; i < ZVP_SOUND_CACHE_SLOTS; i++) {
            zvp_sound_slot_cache_t *c = &mgr->slot_cache[i];
            if (cache_matches_item(c, it) && is_slot_bound_by_us(sys_snd, c->slot)) {
                uint32_t st = 0u;
                if (find_adf_slot_idx(sys_snd, c->slot, &st, NULL) >= 0 &&
                    (st == 3u || st == 6u)) {
                    c->last_seq = ++mgr->cache_seq;
                    bind_sound_table_slot(sys_snd, c->slot);
                    __atomic_store_n(&g_remap_active, true, __ATOMIC_RELEASE);
                    mgr->active_slot = c->slot;
                    mgr->last_f0_bytes = c->f0_bytes;
                    mgr->last_error = 0;
                    mgr->prepared = true;
                    return true;
                }
            }
        }
    }

    size_t max_f0 = ((size_t)ZVP_SOUND_TARGET_RATE * ZVP_SOUND_MAX_SECONDS /
                     ZVP_SOUND_F0_SAMPLES_PER_BLOCK + 2u) * ZVP_SOUND_F0_BLOCK_BYTES;
    uint8_t *f0_buf = (uint8_t *)malloc(max_f0);
    if (!f0_buf) {
        mgr->last_error = -3;
        return false;
    }

    size_t f0_len = 0u;
    int conv_rc = 0;
    if (it->is_builtin) {
        f0_len = zvp_sound_build_builtin_f0(it->builtin_id, f0_buf, max_f0);
        if (f0_len == 0u) {
            conv_rc = -4;
        }
    } else {
        size_t wav_len = 0u;
        uint8_t *wav_buf = read_wav_file_alloc(it->path, &wav_len);
        if (!wav_buf) {
            conv_rc = -5;
        } else {
            conv_rc = zvp_wav_to_f0(wav_buf, wav_len, f0_buf, max_f0, &f0_len);
            free(wav_buf);
        }
    }

    bool ok = false;
    if (conv_rc == 0 && f0_len > 0u) {
        const char *out_path = mgr->active_bin_path[0] ? mgr->active_bin_path : ZVP_SOUND_ACTIVE_BIN_PATH;
        if (write_bin_file(out_path, f0_buf, f0_len)) {
            mgr->last_f0_bytes = (uint32_t)f0_len;
            mgr->last_error = 0;
            mgr->prepared = true;
            pick_and_bind_slot_for_item(mgr, it, (uint32_t)f0_len);
            ok = true;
        } else {
            mgr->last_error = -6;
        }
    } else {
        mgr->last_error = conv_rc;
    }

    free(f0_buf);
    return ok;
}

bool zvp_sound_select(zvp_sound_mgr_t *mgr, uint32_t idx)
{
    if (!mgr) {
        return false;
    }
    if (!mgr->scanned) {
        zvp_sound_scan(mgr);
    }
    if (mgr->count == 0u) {
        return false;
    }
    mgr->selected_idx = idx % mgr->count;
    snprintf(mgr->selected_name, sizeof(mgr->selected_name), "%s", mgr->items[mgr->selected_idx].name);
    mgr->prepared = false;
    return zvp_sound_prepare_current(mgr);
}

bool zvp_sound_select_by_name(zvp_sound_mgr_t *mgr, const char *name)
{
    if (!mgr || !name || !name[0]) {
        return false;
    }
    if (!mgr->scanned) {
        zvp_sound_scan(mgr);
    }
    for (uint32_t i = 0u; i < mgr->count; i++) {
        if (strcmp(mgr->items[i].name, name) == 0) {
            mgr->selected_idx = i;
            snprintf(mgr->selected_name, sizeof(mgr->selected_name), "%s", mgr->items[i].name);
            mgr->prepared = false;
            return zvp_sound_prepare_current(mgr);
        }
    }
    return false;
}

void zvp_sound_prev(zvp_sound_mgr_t *mgr)
{
    if (!mgr) {
        return;
    }
    if (!mgr->scanned || mgr->count <= 2u) {
        zvp_sound_scan(mgr);
    }
    if (mgr->count == 0u) {
        return;
    }
    uint32_t next_idx = (mgr->selected_idx == 0u) ? (mgr->count - 1u) : (mgr->selected_idx - 1u);
    (void)zvp_sound_select(mgr, next_idx);
}

void zvp_sound_next(zvp_sound_mgr_t *mgr)
{
    if (!mgr) {
        return;
    }
    if (!mgr->scanned || mgr->count <= 2u) {
        zvp_sound_scan(mgr);
    }
    if (mgr->count == 0u) {
        return;
    }
    (void)zvp_sound_select(mgr, (mgr->selected_idx + 1u) % mgr->count);
}

static bool play_slot_internal(zvp_sound_mgr_t *mgr, uint64_t now_ms)
{
    uint8_t *sys_snd = __atomic_load_n(&g_sys_snd_data, __ATOMIC_ACQUIRE);
#if !defined(ZVP_HOST_TEST) && !defined(_WIN32)
    if (!sys_snd) {
        sound_logf("sound 警告: g_sys_snd_data 尚未捕获，无法重映射 SoundTable");
        mgr->last_error = -7;
        return false;
    }
#endif
    bind_sound_table_slot(sys_snd, mgr->active_slot);
    __atomic_store_n(&g_remap_active, true, __ATOMIC_RELEASE);

    int32_t err = 0;
    if (hw_play_slot(mgr->active_slot, &err)) {
        mgr->last_error = 0;
        mgr->play_count++;
        mgr->last_play_ms = now_ms;
        return true;
    }
    if (err != 0) {
        mgr->last_error = err;
    }
    return false;
}

bool zvp_sound_play_prepared(zvp_sound_mgr_t *mgr, uint64_t now_ms)
{
    if (!mgr || !mgr->prepared) {
        return false;
    }
    return play_slot_internal(mgr, now_ms);
}

bool zvp_sound_play(zvp_sound_mgr_t *mgr, uint64_t now_ms)
{
    if (!mgr) {
        return false;
    }
    if (!mgr->prepared) {
        if (!zvp_sound_prepare_current(mgr)) {
            return false;
        }
    }
    return play_slot_internal(mgr, now_ms);
}

void zvp_sound_reset(zvp_sound_mgr_t *mgr)
{
    __atomic_store_n(&g_remap_active, false, __ATOMIC_RELEASE);
#if !defined(ZVP_HOST_TEST) && !defined(_WIN32)
    zvp_sound_hook_reset();
#endif

    uint8_t *sys_snd = __atomic_load_n(&g_sys_snd_data, __ATOMIC_ACQUIRE);
    if (sys_snd) {
        for (uint32_t slot = 0u; slot < ZVP_SYSSND_MAX_ID; ++slot) {
            if (!s_saved_slot_valid[slot]) {
                continue;
            }
            uint8_t *entry = sys_snd + ZVP_SYSSND_TABLE_OFF +
                             (size_t)slot * ZVP_SYSSND_ENTRY_STRIDE;
            memcpy(entry, s_saved_slot_entries[slot], ZVP_SYSSND_ENTRY_STRIDE);
#if !defined(ZVP_HOST_TEST) && !defined(_WIN32)
            sys_snd[ZVP_SYSSND_SHUTTER_EN_OFF + slot] = s_saved_slot_sh_en[slot];
#endif
            s_saved_slot_valid[slot] = false;
        }
    } else {
        memset(s_saved_slot_valid, 0, sizeof(s_saved_slot_valid));
    }

    if (!mgr) {
        return;
    }

    const char *bin_path = mgr->active_bin_path[0] ? mgr->active_bin_path : ZVP_SOUND_ACTIVE_BIN_PATH;
#ifdef _WIN32
    (void)remove(bin_path);
#else
    (void)unlink(bin_path);
#endif

    mgr->prepared = false;
    mgr->active_slot = 0x2du;
    mgr->slot_cursor = 0u;
    mgr->cache_seq = 0u;
    mgr->evict_count = 0u;
    memset(mgr->slot_cache, 0, sizeof(mgr->slot_cache));
    mgr->last_f0_bytes = 0u;
    mgr->last_error = 0;
    mgr->play_count = 0u;
    mgr->last_play_ms = 0u;

    uint32_t default_idx = 0u;
    for (uint32_t i = 0u; i < mgr->count; ++i) {
        if (mgr->items[i].is_builtin && mgr->items[i].builtin_id == 0u) {
            default_idx = i;
            break;
        }
    }
    if (mgr->count > 0u) {
        mgr->selected_idx = default_idx;
        snprintf(mgr->selected_name, sizeof(mgr->selected_name), "%s", mgr->items[default_idx].name);
    } else {
        mgr->selected_idx = 0u;
        mgr->selected_name[0] = '\0';
    }
    sound_logf("sound 全量复位完成: 已还原 SoundTable 并清除缓存文件");
}
