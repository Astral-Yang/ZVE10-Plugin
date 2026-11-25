<div align="center">

# ZVE10-Plugin

**让第一代 Sony ZV-E10（`ILCE-ZV-E10`）解锁更多实用功能的轻量化相机插件**

无需破坏原厂系统 · 极简半透明悬浮菜单 · 随时一键还原

<p>
  <img src="https://img.shields.io/badge/适用机型-Sony%20ZV--E10%20(一代)-black?style=flat-square" height="20" alt="适用机型">
  <a href="LICENSE"><img src="https://img.shields.io/badge/开源协议-MIT-blue?style=flat-square" height="20" alt="MIT License"></a>
  <a href="https://github.com/nolan2628/ZVE10-Plugin/stargazers"><img src="https://img.shields.io/github/stars/nolan2628/ZVE10-Plugin?style=flat-square" height="20" alt="GitHub Stars"></a>
  <a href="https://github.com/nolan2628/ZVE10-Plugin/issues"><img src="https://img.shields.io/github/issues/nolan2628/ZVE10-Plugin?style=flat-square" height="20" alt="GitHub Issues"></a>
  <a href="https://discord.gg/wdF3y3xCYR"><img src="https://img.shields.io/badge/Discord-交流群-5865F2?style=flat-square&logo=discord&logoColor=white" height="20" alt="Discord"></a>
  <a href="#赞助支持"><img src="https://img.shields.io/badge/赞助-Token%20支持-ff69b4?style=flat-square&logo=githubsponsors&logoColor=white" height="20" alt="赞助支持"></a>
</p>

[English](README.en.md) · **简体中文**

[功能亮点](#-功能亮点) · [AI Agent 安装](#-如果你是-ai-agent请看这里) · [使用指南](#-使用指南) · [功能进度](#-功能进度) · [安全保障](#-安全保障) · [开发者说明](#️-安装与开发者说明) · [社区交流](#-社区交流) · [赞助支持](#赞助支持) · [致谢](#-致谢开源项目)

</div>

---

## ✨ 功能亮点

> [!NOTE]
> 当前插件处于积极开发阶段，已上线的核心功能均已在实机上验证稳定；菜单中其余入口为后续规划预留。

- **📶 5GHz Wi-Fi 解锁 · 传图更快更稳**  
  突破原厂仅限 2.4GHz 的传输限制，一键开启 5GHz 频段，并支持 `Auto` 自动选择以及 `36 / 40 / 44 / 48 / 149 / 153 / 157 / 161 / 165` 共 9 个常用信道，连接手机或电脑传图更流畅。

- **🔊 自定义快门音效 · 拍摄小悬浮窗**  
  自带 **「清脆快门」** 与 **「电子提示」** 两款预设，也支持直接读取 SD 卡里的 `.WAV` 音频。还可以在取景画面左上角打开迷你小悬浮窗，不遮挡对焦与取景，随时切换音效与试听。

- **🎛️ 极简半透明浮窗 · 按键触控全兼容**  
  半透明黑白高对比界面直接叠加在取景画面上，不遮挡原生拍摄参数。支持手指触控滑动、自拍翻转屏（180° 自动适应方向），也完整兼容机身方向键、`OK` 键与波轮操作。

- **🛡️ 多重安全门禁 · 随时秒回原厂**  
  拔掉 SD 卡或在卡里放一个名为 `ZVP_OFF` 的空文件即可立即停用插件；菜单内也提供一键「安全复位」，瞬间恢复至原厂设置。

---

## 🤖 如果你是 AI Agent，请看这里

> [!IMPORTANT]
> **AI Agent 安装与运维入口**：本仓库已内置完整的安装规范与安全红线。
> - 全局路由入口：[AGENTS.md](AGENTS.md)
> - 安装与排错 Skill：[`.agents/skills/zve10-plugin-install/SKILL.md`](.agents/skills/zve10-plugin-install/SKILL.md)
>
> **执行前必读**：如果你正在帮用户编译、校验或安装本插件到真机，请**先完整阅读** `SKILL.md` 中的检查清单与安全约束（包括：严禁并发执行 USB / `camtool` 命令、编译后必跑 `verify_elf_abi.py` 校验、先执行 `--check` 再 `--stage-tmp` 与 `--enable-preload`）。
>
> **普通用户小贴士**：不会手动命令行也没关系，直接让你的 AI 编程助手（如 Antigravity / Claude Code / Cursor 等）读取本项目的 `.agents/skills/zve10-plugin-install/SKILL.md` 并连接相机 USB，即可由 Agent 一步步帮你完成检测与安装。

---

## 📖 使用指南

### 1. 如何打开与操作菜单
- **呼出 / 关闭菜单**：按下机身顶部 **C1 键** 即可显示或隐藏插件界面。
- **防止日常误触**：在 **「设置与安全」** 中可将 C1 键模式切换为 **「长按 ≥500ms」**。开启后，短按 C1 仍保留你原来的相机功能，长按才呼出插件菜单。
- **操作方式**：
  - **触控操作**：直接点击按钮或上下滑动左侧菜单栏，支持翻折自拍时屏幕 180° 自动转向。
  - **按键操作**：使用机身方向键、波轮及 `OK` 键均可流畅切换并设置所有功能。

### 2. 如何导入自己的快门音效
1. 准备一段 `.wav`（或 `.WAV`）格式的音频文件（可在仓库的 [assets/sounds/](assets/sounds/) 目录直接获取现成的机械快门和提示音示例）。
2. 将音频文件放入 SD 卡的以下任意目录中（推荐第一个）：
   ```text
   /PRIVATE/M4ROOT/GENERAL/SOUNDS/
   /PRIVATE/SONY/PRO/SOUNDS/
   /SOUNDS/
   ```
3. 插卡开机后，进入插件的 **「快门音效」** 页面即可选中、试听并启用；开启 **「拍摄界面小悬浮窗」** 后，在拍照界面左上角就能一键切换不同音效。

> [!TIP]
> **音频格式小贴士**：请使用常见的**未压缩 PCM 编码 WAV 文件**（支持 `8/16/24/32-bit`、单/双声道、`8000–192000 Hz` 采样率），单个文件请控制在 **1 MB 以内、时长不超过 5 秒**（推荐 `0.1 ~ 1.5 秒`，连拍体验更佳）。

---

## 📋 功能进度

插件左侧共规划了 **10 个一级菜单入口**，当前已完成核心安全框架与 3 个实用模块，其余功能将逐步更新：

| 序号 | 菜单名称 | 状态 | 说明 |
| :---: | :--- | :---: | :--- |
| 1 | **创意外观** | 🚧 规划中 | 菜单已预留位置，功能正在开发中 |
| 2 | **LUT** | 🚧 规划中 | 菜单已预留位置，功能正在开发中 |
| 3 | **5GHz WIFI** | ✅ 已可用 | 一键解锁 5GHz 频段，支持 Auto 与 9 个常用信道切换 |
| 4 | **文件共享** | 🚧 规划中 | 菜单已预留位置，计划支持局域网 / FTP 传图 |
| 5 | **快门音效** | ✅ 已可用 | 内置 2 款预设 + SD 卡 WAV 读取 + 拍摄悬浮窗快速试听切换 |
| 6 | **偏色校准** | 🚧 规划中 | 菜单已预留位置，功能正在开发中 |
| 7 | **纵横比** | 🚧 规划中 | 菜单已预留位置，计划提供多种取景参考框/画幅辅助 |
| 8 | **USB外录** | 🚧 规划中 | 菜单已预留位置，功能正在开发中 |
| 9 | **1080P 超采** | 🚧 规划中 | 菜单已预留位置，功能正在开发中 |
| 10 | **设置与安全** | ✅ 已可用 | C1 键唤醒方式切换、翻转屏触控适配、一键全量安全复位 |

---

## 🛡️ 安全保障

本插件在设计时将**相机安全与可逆性**放在首位，提供三种零门槛回退原厂的方式：

1. **拔卡即停**：开机时若未检测到 SD 卡，插件会自动保持休眠，相机以 100% 原厂状态启动。
2. **SD 卡开关（`ZVP_OFF`）**：在 SD 卡根目录新建一个名为 `ZVP_OFF`（无后缀名）的空文件，开机后插件会自动禁用，适合临时借给他人或不想加载插件时使用。
3. **界面一键安全复位**：在 **「设置与安全」** 页面，3 秒内连续点击两次 **「安全复位」**，即可将 Wi-Fi 频段、快门音效和按键设置全部还原为原厂默认状态。

> [!CAUTION]
> **免责声明**：本插件属于非官方扩展工具，仅适用于第一代 **Sony ZV-E10 (`ILCE-ZV-E10`)**，不支持 ZV-E10 II 等其他型号。使用前请妥善备份个人设置，使用过程中产生的一切风险由使用者自行承担。

---

## 🛠️ 安装与开发者说明

> 目前项目仍在迭代中，暂未发布写入相机内置存储的“一键永久安装包”，现阶段通过电脑 USB 连接相机进行临时载入与体验（重启或关闭注入即可恢复原厂）。

> [!WARNING]
> **Windows 连机驱动与屏幕提示必读**：
> 1. **首次连接请先装驱动**：下载 [`zadig-2.9.exe`](https://github.com/pbatard/libwdi/releases/download/v1.5.1/zadig-2.9.exe)，相机开机设为 **大容量存储** 并连接电脑，在 Zadig 中选中相机设备后，驱动版本必须选择 **`libusbK`** 并点击安装。
> 2. **切勿拔掉数据线**：连接 USB 或通信期间，若相机屏幕弹出 **「请检查所连接的设备」**，这属于**完全正常的联机现象，绝对不能拔掉数据线**，保持连接继续操作即可。

<details>
<summary><b>点此展开：编译环境、测试命令、相机载入与项目目录（面向开发者与进阶用户）</b></summary>

### 1. 环境准备
- **仅直接推送使用**：安装 [`uv`](https://docs.astral.sh/uv/)，并使用 `git clone --recursive`（或运行 `git submodule update --init third_party/camtool third_party/pmca`）获取内置调试工具与认证层，即可直接将预编译产物 [`prebuilt/libzve10plugin.so`](prebuilt/libzve10plugin.so) 推送到相机。
- **自行修改编译**（可选）：
  - 主机测试：Linux / WSL2（`cmake`、`gcc`）
  - 交叉编译：`arm-linux-gnueabi-gcc` / `arm-linux-gnueabi-g++`
  - 拉取全部子模块源码（LVGL `v9.2.2` + Camtool + PMCA）：`git clone --recursive ...` 或运行 `git submodule update --init --recursive`

### 2. 主机单元测试与交叉编译（可选）
```bash
# 1. 拉取 LVGL 子模块（仅重新编译需要）
git submodule update --init --recursive

# 2. 运行主机端单元测试
cmake -S . -B build-host -DZVP_BUILD_HOST_TESTS=ON
cmake --build build-host -j
ctest --test-dir build-host --output-on-failure

# 3. 交叉编译适用于 ZV-E10 的 ARMv7 软浮点库
cmake -S . -B build-arm -DCMAKE_TOOLCHAIN_FILE=cmake/zve10_arm_soft.cmake
cmake --build build-arm -j
```
```powershell
# 校验 ELF 与 glibc 2.11.2 兼容性（可校验 build-arm 或 prebuilt 产物）
uv run --with pyelftools python tools/verify_elf_abi.py prebuilt/libzve10plugin.so

# 若修改了 UI 中文文案，重新生成精简字体
uv run --with pillow python tools/gen_zh_font.py
```

### 3. 通过电脑连接相机载入体验
部署脚本默认会自动优先使用 `build-arm/libzve10plugin.so`，若未编译则直接使用仓库内置的 `prebuilt/libzve10plugin.so`，并自动调用内置子模块 `third_party/camtool/cam.py`（也可通过 `--cam-py` / `ZVP_CAM_PY` 指定其他路径）。

1. **配置 Windows USB 驱动（首次需要）**：下载并运行 [`zadig-2.9.exe`](https://github.com/pbatard/libwdi/releases/download/v1.5.1/zadig-2.9.exe)，相机开机且 USB 连接模式设为 **大容量存储** 后接上电脑；在 Zadig 下拉框找到相机设备（如未出现可勾选 `Options -> List All Devices`），将驱动切换安装为 **`libusbK`** 版本。
2. **连接注意**：相机屏幕出现 **「请检查所连接的设备」** 时**属于正常现象，绝对不能拔掉数据线**！保持联机依次执行下列命令：

```powershell
# 1. 检查相机连接状态（只读）
uv run python tools/probe_zve10_runtime.py --check

# 2. 上传插件到相机临时目录
uv run python tools/probe_zve10_runtime.py --stage-tmp

# 3. 启用注入引导（重启相机后生效）
uv run python tools/probe_zve10_runtime.py --enable-preload

# 4. 重启相机并等待重新连线
uv run python tools/probe_zve10_runtime.py --reboot

# 5. 查看运行日志
uv run python tools/probe_zve10_runtime.py --logs

# 6. 关闭注入并恢复原厂默认启动模式（随后 --reboot 重启生效）
uv run python tools/probe_zve10_runtime.py --disable-preload
```

> **提示**：切勿同时并发运行多个 `camtool` / USB 命令，推送、重启与拉取日志请依次执行。

### 4. 目录结构
```text
ZVE10-Plugin/
├── .agents/skills/         # AI Agent 自动化安装与运维 Skill
├── assets/sounds/          # 示例 WAV 快门音效与说明
├── cmake/                  # 交叉编译工具链与符号脚本
├── docs/assets/            # 静态图片资源
├── include/                # 核心头文件
├── prebuilt/               # 预编译好的相机端共享库 (libzve10plugin.so)
├── src/                    # 插件源码与生成字体
├── tests/                  # 单元测试
├── third_party/
│   ├── camtool/            # Sony-ZVE-10-Camtool USB 调试工具 (Git Submodule)
│   ├── lvgl/               # LVGL v9.2.2 界面库 (Git Submodule)
│   └── pmca/               # Sony-PMCA-RE USB 认证层依赖 (Git Submodule)
├── tools/                  # 字库生成、ABI 校验与部署工具
├── AGENTS.md               # AI Agent 入口与安全守则
├── CMakeLists.txt
├── LICENSE
├── README.md
└── README.en.md
```

</details>

---

## 🤝 社区交流

欢迎加入 Discord 社区交流使用体验、反馈问题或讨论新功能想法：

<p>
  <a href="https://discord.gg/wdF3y3xCYR"><img src="https://img.shields.io/badge/Discord-加入交流群-5865F2?style=for-the-badge&logo=discord&logoColor=white" height="28" alt="Discord"></a>
</p>

---

## 赞助支持

如果本项目对你开展索尼相机固件逆向或插件开发研究有所帮助，欢迎通过微信或支付宝扫码赞助支持——**希望多多赞助 token 费用**，用于持续推进 AI 辅助逆向分析、工程开发与设备测试开销：

| 微信支付 (WeChat Pay) | 支付宝 (Alipay) |
| :---: | :---: |
| <img src="docs/assets/sponsor-wechat.jpg" width="220" alt="微信赞助"> | <img src="docs/assets/sponsor-alipay.jpg" width="220" alt="支付宝赞助"> |

---

## 🙏 致谢开源项目

本项目的实现离不开以下优秀开源项目的技术启发与参考：

| 项目 | 开源协议 | 贡献与参考内容 |
| :--- | :---: | :--- |
| [Erlla/sony-a7m4-custom-firmware](https://github.com/erlla/sony-a7m4-custom-firmware) | `GPL-3.0` | 索尼快门音频编码、示例 WAV 音效及 LUT 研究参考 |
| [nolan2628/ZVE10-Astra-Public](https://github.com/nolan2628/ZVE10-Astra-Public) | `MIT` | ZV-E10 屏幕叠加层绘制与按键触控框架基础 |
| [dino2048/ZV-E10-5GHz-WiFi-Unlock](https://github.com/dino2048/ZV-E10-5GHz-WiFi-Unlock) | `MIT` | 一代 ZV-E10 原生 5GHz Wi-Fi 频段与信道解锁参考 |
| [nolan2628/Sony-ZVE-10-Camtool](https://github.com/nolan2628/Sony-ZVE-10-Camtool) | `See repo` | ZV-E10 USB 调试、文件推送与重启通道（内置为 `third_party/camtool` 子模块） |
| [ma1co/Sony-PMCA-RE](https://github.com/ma1co/Sony-PMCA-RE) | `MIT` | Sony USB MSC/Senser 握手与服务认证底层库（内置为 `third_party/pmca` 子模块） |
| [lvgl/lvgl](https://github.com/lvgl/lvgl) | `MIT` | 驱动相机屏幕悬浮菜单的轻量级图形界面库 |

---

<div align="center">
本项目原创代码与文档基于 <a href="LICENSE">MIT License</a> 开源 · 引用的第三方组件遵循其各自开源协议
</div>
