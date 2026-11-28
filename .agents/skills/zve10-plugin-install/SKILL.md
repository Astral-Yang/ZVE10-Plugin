---
name: zve10-plugin-install
description: >-
  Automated installation, ARM build/ABI verification, USB staging, preload setup,
  log diagnostics, and stock rollback skill for ZVE10-Plugin on Sony ZV-E10 (Gen 1).
  Use when a user asks an AI Agent to install, deploy, build, verify, troubleshoot,
  or uninstall ZVE10-Plugin onto their Sony ZV-E10 camera.
---

# ZVE10-Plugin 自动化安装与运维 Skill

本 Skill 用于指导 AI Agent 安全地将 **ZVE10-Plugin** 编译、校验、推送并启用到一台 **第一代 Sony ZV-E10（`ILCE-ZV-E10`）** 相机上，或在需要时安全恢复至原厂状态。

---

## 1. 前置检查与依赖准备

在向相机执行任何命令前，请先向用户确认以下 3 项条件（缺一则先引导用户准备）：

1. **机型确认**：仅支持第一代 **Sony ZV-E10 (`ILCE-ZV-E10`)**。若用户是 ZV-E10 II 或其他型号，**立即拒绝安装**。
2. **相机连接与 Windows 驱动状态**：
   - 相机已插入 SD 卡并开机；
   - 相机菜单：`设置 (Setup) -> USB 连接模式 -> 大容量存储 (Mass Storage)`；
   - 通过数据线直连电脑（避免使用不稳定扩展坞）；
   - **Windows 首次使用驱动（Zadig `libusbK`）**：如果 `--check` 检测不到设备，请先引导用户下载 [`zadig-2.9.exe`](https://github.com/pbatard/libwdi/releases/download/v1.5.1/zadig-2.9.exe)，连接相机后在 Zadig 中勾选 `Options -> List All Devices`，分别对相机 **大容量存储 (`054C:0D95`)** 与 **Sony 服务模式 (`054C:0336`)** **安装 `libusbK` 版本驱动**（切勿使用 MTP / PC 遥控模式 `0x0994`）；
   - **务必提前告知用户**：连接 USB 后相机屏幕出现 **「请检查所连接的设备」** 属于**正常现象，绝对不能拔掉数据线**，保持连接即可。
3. **宿主机工具依赖**：
   - 必须已安装 [`uv`](https://docs.astral.sh/uv/)；
   - 若需要从源码编译 `.so`，还需具备 WSL2 / Linux 环境下的 `cmake` 与 `arm-linux-gnueabi-gcc` / `arm-linux-gnueabi-g++`；
   - **内置调试与推机工具 `third_party/camtool` 与认证层 `third_party/pmca`**：分别作为 Git Submodule 挂载了 [`Sony-ZVE-10-Camtool`](https://github.com/nolan2628/Sony-ZVE-10-Camtool) 与 [`ma1co/Sony-PMCA-RE`](https://github.com/ma1co/Sony-PMCA-RE)。若对应目录为空，先在仓库根目录执行 `git submodule update --init --recursive` 拉取。`tools/probe_zve10_runtime.py` 会自动注入这两个子模块路径（也可用 `--cam-py` / `ZVP_CAM_PY` / `PMCA_ROOT` 覆盖）。

---

## 2. 必须严格遵守的安全红线

1. **严禁并发操作 USB / `camtool`**：同一时刻只能运行一个 `probe_zve10_runtime.py` 或 `cam.py` 进程；推送、启用、重启、等待、读取日志必须**单命令严格串行**执行。
2. **推送前必须校验 ELF ABI**：只要重新编译了 `build-arm/libzve10plugin.so`，推送前必须执行 `tools/verify_elf_abi.py` 并确认输出 `ELF/ABI/GLIBC 约束全部校验通过`（目标板仅支持 ARMv7 soft-float EABI5 与 `<= GLIBC_2.11.2`）。
3. **禁止直接覆写内存已映射文件，禁止走 `0xff01` 直接写 2.03 文件**：切勿手动 `cam.py put` 覆写 `/tmp/libzve10plugin.so`，必须使用 `probe_zve10_runtime.py --stage-tmp`（统一通过 `/tmp/jiritsu/` 中转 + 目标目录临时文件原子 `mv`，全面兼容 2.02/2.03）。
4. **先体检再启用**：首次启用前务必运行 `--check`，确认 USB / Jiritsu / `sh` 通道与 `/setting/kemco.txt` 状态正常（脚本不依赖 `od` 也不因缺失 `backup/*.bin` 中断），且原厂基线字节校验通过（`libNetService.so` 为 `90 b5 95 b0`，`CautionConfig.so` 为 `20`）。
5. **严禁手动改写 `/setting/kemco.txt`**：如需启用冷启动 (`warm.mode=0`) 或还原暖启动 (`warm.mode=2`)，只能调用 `--enable-coldboot` / `--restore-warmboot`（内置首次原厂基线备份 `backups/kemco/kemco.txt.orig`、单字节锚点校对与写后 SHA-256 回读自动回滚保护）。

---

## 3. 标准安装流程（按顺序执行）

### Step 1：确认预编译产物（或按需重新编译）

1. **优先使用仓库内置预编译产物**（推荐，普通用户无需安装交叉编译工具链）：
   检查 `prebuilt/libzve10plugin.so` 或 `build-arm/libzve10plugin.so` 是否存在，并先执行 ABI 校验：
   ```powershell
   uv run --with pyelftools python tools/verify_elf_abi.py prebuilt/libzve10plugin.so
   ```
   > 若通过校验且未修改 C 源码，可直接跳至 **Step 2**（`tools/probe_zve10_runtime.py --stage-tmp` 会自动优先选用 `build-arm/libzve10plugin.so`，不存在时自动回退使用 `prebuilt/libzve10plugin.so`）。

2. **如用户修改了源码或需从零编译**：
   先确认已拉取 LVGL 子模块，再在 Linux / WSL2 中运行主机单测与 ARM 交叉编译：
   ```bash
   git submodule update --init --recursive

   cmake -S . -B build-host -DZVP_BUILD_HOST_TESTS=ON
   cmake --build build-host -j
   ctest --test-dir build-host --output-on-failure

   cmake -S . -B build-arm -DCMAKE_TOOLCHAIN_FILE=cmake/zve10_arm_soft.cmake
   cmake --build build-arm -j
   ```
   然后执行校验：
   ```powershell
   uv run --with pyelftools python tools/verify_elf_abi.py build-arm/libzve10plugin.so
   ```

> 仅在看到 `[通过] libzve10plugin.so ELF/ABI/GLIBC 约束全部校验通过！` 后方可继续下一步。

### Step 2：检查相机 USB 连接与基线状态（只读）

```powershell
uv run python tools/probe_zve10_runtime.py --check
```

若找不到 `cam.py`，请显式传入路径，例如：
```powershell
uv run python tools/probe_zve10_runtime.py --cam-py <path-to-cam.py> --check
```

核对输出：
- `cam.py doctor` 正常连接；
- `preload=` 当前状态（初次为 `NONE` 或已配置值）；
- `verify firmware bytes on disk` 与期望字节一致（`90 b5 95 b0` 与 `20`）。

### Step 3：推送插件至相机临时区

```powershell
uv run python tools/probe_zve10_runtime.py --stage-tmp
```

确认输出末尾显示 `/tmp/libzve10plugin.so` 权限为 `-rwxr-xr-x` 且大小约 900KB 左右。

### Step 4：启用 `preload` 注入引导

若 `--check` 中发现 `preload` 尚未包含 `/tmp/libzve10plugin.so`，执行：

```powershell
uv run python tools/probe_zve10_runtime.py --enable-preload
```

> 说明：该命令会执行 `change_mode.sh p /usr/tool/libmcheckb.so:/tmp/libzve10plugin.so`，不会写入只读 `/usr` 分区。

### Step 4.5（2.02 / 2.03 零售机开启持久加载，约 1 秒极速冷启）：切换 `warm.mode=0`

如果 `--check` 或 `--enable-preload` 提示 `/setting/kemco.txt` 当前为 `warm.mode=2`（2.02/2.03 出厂暖启动镜像机制会绕过 `bootin.elf`，导致重启不生效），向用户确认开启冷启动持久模式后执行：

```powershell
uv run python tools/probe_zve10_runtime.py --enable-coldboot
```

> 说明：脚本会自动备份原厂 `/setting/kemco.txt` 至 `backups/kemco/kemco.txt.orig`，仅单字节将 `warm.mode=2` 改为 `warm.mode=0` 并执行 SHA-256 回读核对。

### Step 5：重启相机并等待重连，拉取运行日志验证

使用串行命令执行重启与读取日志（`--reboot` 会串行发送 `reboot --confirm`）：

```powershell
uv run python tools/probe_zve10_runtime.py --reboot; if ($LASTEXITCODE -eq 0) { uv run python tools/probe_zve10_runtime.py --logs }
```

**成功判据**：日志末尾应包含类似输出：
- `GEN_CacheInsImg 640x480 RGBA4444 画布初始化完成`
- `zve10plugin UI主线程激活成功: tid=... c1_key=0x150102a0 canvas=1 sounds=...`

随后即可提醒用户在相机上按 **C1 键** 打开插件菜单。

### Step 6（可选）：导入示例快门音效与自定义 LUT 滤镜包到 SD 卡

- **自定义快门音**：将 `assets/sounds/` 中的 `.WAV` 文件复制到 SD 卡 `/PRIVATE/M4ROOT/GENERAL/SOUNDS/` 目录下。
- **自定义 LUT 滤镜包**：将 `ZVLOOK01` 格式的 `.bin` 滤镜包复制到 SD 卡 `/ZVC/` 或 `/PRIVATE/SONY/PRO/LUT/` 目录下，在插件「滤镜」页点击顶栏 `SD导入` 图标即可热加载。

---

## 4. 故障排查与彻底恢复原厂（卸载）

- **用户想要恢复原厂引导 / 卸载插件**：
  ```powershell
  uv run python tools/probe_zve10_runtime.py --disable-preload
  # 若先前启用过 --enable-coldboot 且想同时恢复原厂 warm.mode=2 暖启动：
  uv run python tools/probe_zve10_runtime.py --restore-warmboot
  uv run python tools/probe_zve10_runtime.py --reboot
  ```
  执行完成后，相机即重启切回原厂 `mode 3` 无注入状态。
- **诊断注入/看门狗状态或清理崩溃计数**：
  ```powershell
  uv run python tools/probe_zve10_runtime.py --preload-status
  uv run python tools/probe_zve10_runtime.py --reset-boot-guard
  ```
- **日志显示 `NO_LOG`**：
  - 使用 `--preload-status` 检查是否仍为 `warm.mode=2`（需运行 `--enable-coldboot`），或是否缺少 `/tmp/libzve10plugin.so`、`dmode` 非 `3p`、触发了看门狗自熔断 (`trip_count > 0`)。
- **日志显示 `卡控门禁拦截启动`**：
  - `DISABLED_NO_CARD`：请插入 SD 卡后再开机；
  - `DISABLED_KILL_FILE`：SD 卡根目录或 `/tmp/` 存在 `ZVP_OFF` 文件，删除后重启即可恢复。
- **菜单按键无反应或触控错位**：
  - 检查是否在 `设置与安全` 中把 C1 改为了 `长按 >=500ms`（需长按 C1 半秒）；
  - 可在 `设置与安全` 中连续两次点击 `安全复位 (全量恢复默认)` 一键重置。
