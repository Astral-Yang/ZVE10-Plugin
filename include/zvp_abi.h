#ifndef ZVP_ABI_H
#define ZVP_ABI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Sony ZV-E10 (ILCE-ZV-E10 / Gen 1 BIONZ X) 离线反汇编已确认之二进制偏移与签名
 * 注意：运行期仅在校验基址、机器码前缀并成功 dlsym 后才触发操作
 * ========================================================================== */

#define ZVP_SCREEN_WIDTH  640
#define ZVP_SCREEN_HEIGHT 480

/* libNetService.so :: isValidWifi5GHz() Thumb-2 补丁点 */
#define ZVP_OFF_NETSVC_ISVALID_5GHZ  0x00065AB8u
static const uint8_t ZVP_ORIG_ISVALID_5GHZ[4]  = { 0x90u, 0xB5u, 0x95u, 0xB0u }; /* push {r4,r7,lr}; sub sp, #0x54 */
static const uint8_t ZVP_PATCH_ISVALID_5GHZ[4] = { 0x01u, 0x20u, 0x70u, 0x47u }; /* movs r0, #1; bx lr */

/* CautionConfig.so :: Wi-Fi 国家限制表 TBB case 12 (0x20 TypeRK -> 0x1B TypeNB) */
#define ZVP_OFF_CAUTION_WIFI_CASE    0x00D778E4u
#define ZVP_ORIG_CAUTION_BYTE        0x20u
#define ZVP_PATCH_CAUTION_BYTE       0x1Bu

/* libbackup.so Backup ID 常量（所有写操作均不调用 Backup_sync_subsystem） */
#define ZVP_BKID_SUBSYS_NET          0x013Eu
#define ZVP_BKID_WIFI_5G_ENABLE      0x013E003Au /* 实为 5GHz Ch36 AP 允许位（CN 出厂=0，149..165=1），勿作开关覆写 */
#define ZVP_BKID_WIFI_AP_CH_BAND     0x013E0061u /* 2B 小端格式：[0]=band(1=2.4G,2=5G), [1]=channel(0xFF=Auto) */
#define ZVP_WIFI_BAND_24G            0x01u
#define ZVP_WIFI_BAND_5G             0x02u
#define ZVP_WIFI_CH_AUTO             0xFFu

/* ZV-E10 无线 NVRAM 地区配置与原生频段菜单开关（参照 dino2048 方案适配中国大陆 CN1/CN2） */
#define ZVP_BKID_SUBSYS_NVRAM        0x00A5u
#define ZVP_BKID_WIFI_NVRAM_GO       0x00A5000Bu /* 热点(GO) NVRAM 编号：原厂=0x09(XR/83 仅2.4G), CN=0x02(XR/76) */
#define ZVP_BKID_WIFI_NVRAM_STA      0x00A5000Cu /* 访问点扫描/路由器(STA) NVRAM：原厂=0x09(XR/83), CN=0x16(22,XR/143) */
#define ZVP_WIFI_NVRAM_CN_GO         0x02u
#define ZVP_WIFI_NVRAM_CN_STA        0x16u       /* 十进制 22 */

#define ZVP_BKID_SUBSYS_SYS          0x0107u
#define ZVP_BKID_WIFI_BAND_MENU      0x01071C54u /* 原生 2.4G/5G 频段菜单启用属性：1=启用, 0=禁用 */

/* libObj.so 关键原生偏移与结构体成员布局（已由反汇编实证确认） */
#define ZVP_CACHEIMG_ALLOC_SIZE      0x0400u /* GEN_CacheInsImg 安全上界（已知使用至 +0x20e） */
#define ZVP_CACHEIMG_OFF_IMGDATA     0x015Cu /* 内嵌 libra::ImageData */
#define ZVP_CACHEIMG_OFF_SPRITE      0x0164u /* 内嵌 ux::gfxeng::Sprite */
#define ZVP_CACHEIMG_OFF_INIT_FLAG1  0x020Cu
#define ZVP_CACHEIMG_OFF_INIT_FLAG2  0x020Du
#define ZVP_CACHEIMG_OFF_INIT_DONE   0x020Eu
#define ZVP_GFXIMG_OFF_BMP           0x0018u /* GraphicsImage::sys_getInternalObject 实证偏移 */

/* Sprite 内部刷新函数（libObj.so 未导出但可通过前缀机器码校验） */
#define ZVP_OFF_SPRITE_SET_IMG       0x00624DD5u /* Thumb (+1) */
#define ZVP_OFF_SPRITE_INVALIDATE    0x00624EB5u /* Thumb (+1) */
#define ZVP_WIDGETBASE_ONANIMATE_SLOT_DEFAULT 32u /* 0x80 / 4 */

/* CreateImageParam 像素格式：7 = RGBA4444 (4-4-4-4 带 Alpha 透明通道) */
#define ZVP_PIXEL_FMT_RGBA4444       7

/* PrFILE2 / libInfraFsys.so 盘符掩码与禁用门禁文件
 * 注意：ZV-E10 真机上 A: (bit 0) 为机内虚拟 INFO/LICENSE 盘（无论是否插卡均存在），
 *       物理 SD 卡槽为 B: (bit 1 = 0x02)。 */
#define ZVP_FSYS_DRIVE_A_BIT         0x01u
#define ZVP_FSYS_DRIVE_B_BIT         0x02u
#define ZVP_FSYS_DRIVE_SD_MASK       0x02u
#define ZVP_GATE_DISABLE_FILE_NAME   "ZVP_OFF"
#define ZVP_GATE_UNINSTALL_NAME      "ZVP_UNINSTALL"
#define ZVP_GATE_DISABLE_PATH_FSYS   "B:/ZVP_OFF"
#define ZVP_GATE_DISABLE_PATH_POSIX1 "/mnt/sd/ZVP_OFF"
#define ZVP_GATE_DISABLE_PATH_POSIX2 "/tmp/ZVP_OFF"

/* 启动看门狗自熔断与持久化启动模式文件 */
#define ZVP_BOOT_GUARD_PATH          "/tmp/zvp_boot_guard"
#define ZVP_MODE_DMODE_PATH          "/setting/mode/dmode"
#define ZVP_MODE_PRELOAD_PATH        "/setting/mode/preload"
#define ZVP_BOOT_GUARD_MAX_FAILS     2u
#define ZVP_BOOT_GUARD_HEALTHY_MS    5000u

/* ============================================================================
 * Sony _InfraInputEvent 结构体布局（实机与 touch_bridge.c 一致，24 字节）
 * ========================================================================== */
typedef struct zvp_infra_input_event {
    uint32_t w0;
    uint32_t w1;
    uint32_t w2;
    uint32_t w3;
    uint32_t w4;
    uint32_t w5;
} zvp_infra_input_event_t;

/* 实机日志 + viewUnified2.so 0xc2c384..0xc2c848 确认的 ZV-E10 面板按键、拨轮与触摸事件常量 */
#define ZVP_KEY_ENTER                0x15010040u /* KEY_TACT_ENTER (0x01ac) 方向键中心 OK 键 */
#define ZVP_KEY_UP                   0x15010050u /* KEY_TACT_UP    (0x0003) */
#define ZVP_KEY_DOWN                 0x15010060u /* KEY_TACT_DOWN  (0x0030) */
#define ZVP_KEY_RIGHT                0x15010070u /* KEY_TACT_RIGHT (0x01af) */
#define ZVP_KEY_LEFT                 0x15010080u /* KEY_TACT_LEFT  (0x0015) */
#define ZVP_KEY_PLAY                 0x150100D0u /* KEY_TACT_PLAY  (0x01f7) */
#define ZVP_KEY_DELETE               0x150100E0u /* KEY_TACT_DELETE(0x01f8) */
#define ZVP_KEY_MENU                 0x150100F0u /* KEY_TACT_MENU  (0x01ab) */
#define ZVP_KEY_C1_DEFAULT           0x150102A0u /* 实机 C1 主事件码 */
#define ZVP_KEY_C1_DEFOCUS_AUX       0x15010310u /* 实机 C1 伴生散焦功能码 */

/* ZV-E10 控制拨轮 / 机顶波轮事件码（正向与 180° 翻转态） */
#define ZVP_KEY_DIAL_TOP             0x15070030u /* hw 0x10000013 */
#define ZVP_KEY_DIAL_WHEEL           0x15070040u /* hw 0x10000016 */
#define ZVP_KEY_DIAL_TOP_R180        0x150700D0u
#define ZVP_KEY_DIAL_WHEEL_R180      0x150701C0u

/* 兼容旧版 16-bit 键码备选 */
#define ZVP_KEY_LEGACY_UP            0x0003u
#define ZVP_KEY_LEGACY_DOWN          0x0030u
#define ZVP_KEY_LEGACY_LEFT          0x0015u
#define ZVP_KEY_LEGACY_RIGHT         0x01AFu
#define ZVP_KEY_LEGACY_ENTER         0x01ACu
#define ZVP_KEY_LEGACY_MENU          0x01ABu

#define ZVP_EV_W0_TOUCH_MAGIC        0x11013001u
#define ZVP_EV_TYPE_TOUCH            0x0002u

/* libObj.so 0x11cb80 / 0x11e6ae 实测布局：w1 高16位为 X (0..639)，低16位为 Y (0..479) */
#define ZVP_TOUCH_UNPACK_X(w1)       ((int16_t)(((uint32_t)(w1) >> 16) & 0xFFFFu))
#define ZVP_TOUCH_UNPACK_Y(w1)       ((int16_t)((uint32_t)(w1) & 0xFFFFu))

/* viewUnified2.so 0x429b14 私有回调直接送入 InputService::createEvent 的中心 OK 键事件码 */
#define ZVP_EV_ENTER_RELEASE         (ZVP_KEY_ENTER | 0x1u) /* 0x15010041u */
#define ZVP_EV_ENTER_PRESS           (ZVP_KEY_ENTER | 0x2u) /* 0x15010042u */

/* Sony libbackup.so / libInfraFsys.so 原生签名 */
typedef int      (*zvp_fn_backup_read_t)(uint32_t bkid, void *buf);
typedef uint32_t (*zvp_fn_backup_datasize_t)(uint32_t bkid);
typedef int      (*zvp_fn_backup_write_t)(uint32_t subsys, uint32_t bkid, const void *buf);
typedef uint32_t (*zvp_fn_fsys_mounts_t)(void);
typedef int      (*zvp_fn_fsys_open_t)(const char *path, int *p_handle, int flags);
typedef int      (*zvp_fn_fsys_close_t)(int handle);

#ifdef __cplusplus
}
#endif

#endif /* ZVP_ABI_H */
