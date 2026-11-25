<div align="center">

# ZVE10-Plugin

**A lightweight on-camera plugin that unlocks extra features for the Sony ZV-E10 (Gen 1, `ILCE-ZV-E10`)**

Non-destructive · Clean semi-transparent menu · Instant safe rollback

<p>
  <img src="https://img.shields.io/badge/Camera-Sony%20ZV--E10%20(Gen%201)-black?style=flat-square" height="20" alt="Target Camera">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue?style=flat-square" height="20" alt="MIT License"></a>
  <a href="https://github.com/nolan2628/ZVE10-Plugin/stargazers"><img src="https://img.shields.io/github/stars/nolan2628/ZVE10-Plugin?style=flat-square" height="20" alt="GitHub Stars"></a>
  <a href="https://github.com/nolan2628/ZVE10-Plugin/issues"><img src="https://img.shields.io/github/issues/nolan2628/ZVE10-Plugin?style=flat-square" height="20" alt="GitHub Issues"></a>
  <a href="https://discord.gg/wdF3y3xCYR"><img src="https://img.shields.io/badge/Discord-Join%20Community-5865F2?style=flat-square&logo=discord&logoColor=white" height="20" alt="Discord"></a>
  <a href="#sponsor"><img src="https://img.shields.io/badge/Sponsor-Token%20Support-ff69b4?style=flat-square&logo=githubsponsors&logoColor=white" height="20" alt="Sponsor"></a>
</p>

**English** · [简体中文](README.md)

[Key Features](#-key-features) · [For AI Agents](#-if-you-are-an-ai-agent-start-here) · [User Guide](#-user-guide) · [Feature Roadmap](#-feature-roadmap) · [Safety](#-safety--recovery) · [Developer Guide](#️-installation--developer-guide) · [Community](#-community) · [Sponsor](#sponsor) · [Acknowledgments](#-acknowledgments)

</div>

---

## ✨ Key Features

> [!NOTE]
> This plugin is under active development. Released features have been verified on real camera hardware; other entries in the menu are placeholders for upcoming updates.

- **📶 5GHz Wi-Fi Unlock · Faster Transfers**  
  Break free from the stock 2.4GHz speed cap. Enable 5GHz Wi-Fi with one tap and choose between `Auto` or 9 channels (`36, 40, 44, 48, 149, 153, 157, 161, 165`) for faster, more reliable transfers to your phone or computer.

- **🔊 Custom Shutter Sounds · Shooting Mini Widget**  
  Includes 2 built-in presets (**Crisp Shutter** and **Electronic Chime**) and loads your own `.WAV` files straight from the SD card. You can also turn on a compact floating widget in the top-left corner of the shooting view to switch and preview sounds on the fly.

- **🎛️ Clean Overlay UI · Touch & Button Friendly**  
  A sleek semi-transparent monochrome menu sits over your live view without hiding camera settings. Supports touch scrolling, automatic 180° flip-screen orientation adaptation for vlogging, as well as physical D-pad, `OK`, and dial controls.

- **🛡️ Built-in Safety · Back to Stock Anytime**  
  Simply remove the SD card or place an empty `ZVP_OFF` file on it to bypass the plugin on boot. You can also trigger a one-click Full Safe Reset right inside the menu.

---

## 🤖 If You Are an AI Agent, Start Here

> [!IMPORTANT]
> **AI Agent Installation & Deployment Entry**: This repository ships a dedicated Agent Skill with full safety guardrails.
> - Root routing guide: [AGENTS.md](AGENTS.md)
> - Install & troubleshooting Skill: [`.agents/skills/zve10-plugin-install/SKILL.md`](.agents/skills/zve10-plugin-install/SKILL.md)
>
> **Read Before Acting**: If you are helping a user build, verify, or install this plugin onto their ZV-E10, **read `SKILL.md` in full first** and strictly follow its guardrails (never run concurrent USB / `camtool` processes, always validate newly built binaries with `verify_elf_abi.py`, and always run `--check` before `--stage-tmp` and `--enable-preload`).
>
> **Tip for End Users**: Not comfortable with command-line tools? Just connect your camera over USB and ask your AI coding assistant (Antigravity, Claude Code, Cursor, etc.) to follow `.agents/skills/zve10-plugin-install/SKILL.md` to guide and automate the setup for you.

---

## 📖 User Guide

### 1. Opening & Navigating the Menu
- **Toggle Menu**: Press the **C1 button** on top of your camera to show or hide the plugin menu.
- **Avoid Accidental Presses**: Inside **Settings & Safety**, switch the C1 button mode to **Long-Press (≥500ms)** so a quick press keeps your original camera shortcut while a long press opens the plugin.
- **Controls**:
  - **Touch**: Tap buttons or swipe the left sidebar; touch coordinates automatically adapt when the screen is flipped 180° forward.
  - **Physical Controls**: Navigate smoothly with the D-pad, control dials, and `OK` button.

### 2. Adding Your Own Shutter Sounds
1. Prepare a `.wav` (or `.WAV`) audio file. Ready-to-use sample files are available in [assets/sounds/](assets/sounds/).
2. Copy the file into one of these folders on your SD card (the first path is recommended):
   ```text
   /PRIVATE/M4ROOT/GENERAL/SOUNDS/
   /PRIVATE/SONY/PRO/SOUNDS/
   /SOUNDS/
   ```
3. Insert the card, turn on the camera, and open **Shutter Sound** in the plugin menu to select, preview, and enable it. Turn on **Shooting Mini Widget** if you want quick switching right from the shooting screen.

> [!TIP]
> **Audio Requirements**: Use standard **uncompressed PCM WAV** files (`8/16/24/32-bit`, mono or stereo, `8000–192000 Hz`). Keep each file **under 1 MB and 5 seconds** (`0.1s – 1.5s` works best for continuous shooting).

---

## 📋 Feature Roadmap

The main menu has **10 slots**. Core safety tools and 3 major modules are ready to use, while the remaining entries are reserved for future releases:

| # | Menu Entry | Status | Description |
| :---: | :--- | :---: | :--- |
| 1 | **Creative Look** | 🚧 Planned | Reserved menu slot; coming in a future update |
| 2 | **LUT** | 🚧 Planned | Reserved menu slot; coming in a future update |
| 3 | **5GHz WIFI** | ✅ Ready | One-tap 5GHz unlock with `Auto` and 9 selectable channels |
| 4 | **File Sharing** | 🚧 Planned | Reserved menu slot; planned for LAN / FTP photo transfer |
| 5 | **Shutter Sound** | ✅ Ready | 2 built-in presets, SD card WAV support, and shooting mini widget |
| 6 | **Color Calibration** | 🚧 Planned | Reserved menu slot; coming in a future update |
| 7 | **Aspect Ratio** | 🚧 Planned | Reserved menu slot; planned for custom frame guides |
| 8 | **USB Recording** | 🚧 Planned | Reserved menu slot; coming in a future update |
| 9 | **1080p Oversampling** | 🚧 Planned | Reserved menu slot; coming in a future update |
| 10 | **Settings & Safety** | ✅ Ready | C1 trigger mode, 180° flip adaptation, and full safe reset |

---

## 🛡️ Safety & Recovery

Safety and reversibility come first. You can return to 100% stock camera behavior at any time in three easy ways:

1. **No Card, No Plugin**: If no SD card is inserted at boot, the plugin stays completely inactive.
2. **SD Card Kill-Switch (`ZVP_OFF`)**: Create an empty file named `ZVP_OFF` (no extension) in the root folder of your SD card to disable the plugin on startup.
3. **One-Click Safe Reset**: In **Settings & Safety**, tap **Safe Reset** twice within 3 seconds to revert Wi-Fi settings, shutter sounds, and button preferences back to stock defaults.

> [!CAUTION]
> **Disclaimer**: This is an unofficial experimental plugin built exclusively for the first-generation **Sony ZV-E10 (`ILCE-ZV-E10`)**. It does not support the ZV-E10 II or other camera models. Back up your camera settings before use; you assume all risks.

---

## 🛠️ Installation & Developer Guide

> A permanent standalone installer is not yet available. Right now, the plugin is loaded temporarily over USB from a computer (rebooting or disabling preload returns the camera to stock).

> [!WARNING]
> **Important USB Driver & On-Camera Prompt Notice (Windows)**:
> 1. **Install `libusbK` Driver via Zadig**: Download [`zadig-2.9.exe`](https://github.com/pbatard/libwdi/releases/download/v1.5.1/zadig-2.9.exe), connect your powered-on camera in **Mass Storage** mode, select the camera in Zadig, and **install the `libusbK` driver**.
> 2. **Do NOT Unplug the Cable**: When connecting USB or communicating with the camera, seeing **"Check the connected device"** / **「请检查所连接的设备」** on the camera screen is **completely normal — NEVER unplug the USB cable**. Keep it connected and proceed.

<details>
<summary><b>Click to expand: Build instructions, tests, USB staging commands & repository structure</b></summary>

### 1. Prerequisites
- **Direct Camera Loading (No Compiler Needed)**: Install [`uv`](https://docs.astral.sh/uv/) and clone with `--recursive` (or run `git submodule update --init third_party/camtool third_party/pmca`) to fetch the bundled USB debugger and auth layer, then push the prebuilt [`prebuilt/libzve10plugin.so`](prebuilt/libzve10plugin.so) directly.
- **Building from Source (Optional)**:
  - Host tests: Linux / WSL2 (`cmake`, `gcc`)
  - ARM cross-build: `arm-linux-gnueabi-gcc` / `arm-linux-gnueabi-g++`
  - Fetch all submodules (LVGL `v9.2.2` + Camtool + PMCA): `git clone --recursive ...` or run `git submodule update --init --recursive`

### 2. Build & Verify (Optional)
```bash
# 1. Fetch LVGL submodule (required only when building from source)
git submodule update --init --recursive

# 2. Run host unit tests
cmake -S . -B build-host -DZVP_BUILD_HOST_TESTS=ON
cmake --build build-host -j
ctest --test-dir build-host --output-on-failure

# 3. Cross-compile for ZV-E10 (ARMv7 soft-float)
cmake -S . -B build-arm -DCMAKE_TOOLCHAIN_FILE=cmake/zve10_arm_soft.cmake
cmake --build build-arm -j
```
```powershell
# Verify ELF & glibc 2.11.2 ABI compatibility (prebuilt or build-arm)
uv run --with pyelftools python tools/verify_elf_abi.py prebuilt/libzve10plugin.so

# Regenerate subset fonts if UI strings were edited
uv run --with pillow python tools/gen_zh_font.py
```

### 3. Load onto Camera via USB
By default, the helper script prefers `build-arm/libzve10plugin.so` if present, otherwise uses [`prebuilt/libzve10plugin.so`](prebuilt/libzve10plugin.so), and invokes the bundled submodule `third_party/camtool/cam.py` (overridable via `--cam-py` / `ZVP_CAM_PY`).

1. **Install Windows USB Driver (First Time)**: Download [`zadig-2.9.exe`](https://github.com/pbatard/libwdi/releases/download/v1.5.1/zadig-2.9.exe), turn on your camera with USB Connection set to **Mass Storage**, connect it to the PC, select the camera in Zadig (`Options -> List All Devices` if hidden), and install the **`libusbK`** driver.
2. **Keep Cable Connected**: If the camera screen displays **"Check the connected device"** (**「请检查所连接的设备」**), this is **normal — do NOT unplug the USB cable**. Proceed with the commands below:

```powershell
# 1. Check connection (read-only)
uv run python tools/probe_zve10_runtime.py --check

# 2. Push plugin to temporary camera storage
uv run python tools/probe_zve10_runtime.py --stage-tmp

# 3. Enable preload (takes effect after reboot)
uv run python tools/probe_zve10_runtime.py --enable-preload

# 4. Reboot camera and wait for reconnection
uv run python tools/probe_zve10_runtime.py --reboot

# 5. View runtime logs
uv run python tools/probe_zve10_runtime.py --logs

# 6. Disable preload and restore stock boot (then run --reboot)
uv run python tools/probe_zve10_runtime.py --disable-preload
```

> **Important**: Never run multiple `camtool` / USB commands concurrently. Always serialize stage, reboot, and log commands.

### 4. Repository Structure
```text
ZVE10-Plugin/
├── .agents/skills/         # AI Agent automated install & troubleshooting skill
├── assets/sounds/          # Sample WAV shutter sounds & placement guide
├── cmake/                  # Cross-build toolchain & version scripts
├── docs/assets/            # Static image assets
├── include/                # Core headers
├── prebuilt/               # Prebuilt camera shared library (libzve10plugin.so)
├── src/                    # Plugin sources & generated UI fonts
├── tests/                  # Host unit tests
├── third_party/
│   ├── camtool/            # Sony-ZVE-10-Camtool USB debugger (Git Submodule)
│   ├── lvgl/               # LVGL v9.2.2 graphics library (Git Submodule)
│   └── pmca/               # Sony-PMCA-RE USB auth dependency (Git Submodule)
├── tools/                  # Font generator, ABI checker & camera deployment helper
├── AGENTS.md               # AI Agent routing & safety guardrails
├── CMakeLists.txt
├── LICENSE
├── README.md
└── README.en.md
```

</details>

---

## 🤝 Community

Have feedback, feature ideas, or questions? Come chat with us on Discord:

<p>
  <a href="https://discord.gg/wdF3y3xCYR"><img src="https://img.shields.io/badge/Discord-Join%20Community-5865F2?style=for-the-badge&logo=discord&logoColor=white" height="28" alt="Discord"></a>
</p>

---

## Sponsor

If this project helps your research in Sony camera reverse engineering or cross-generation firmware porting, feel free to support ongoing R&D, hardware wear, and **AI token costs** via WeChat Pay or Alipay:

| WeChat Pay | Alipay |
| :---: | :---: |
| <img src="docs/assets/sponsor-wechat.jpg" width="220" alt="WeChat Sponsor"> | <img src="docs/assets/sponsor-alipay.jpg" width="220" alt="Alipay Sponsor"> |

---

## 🙏 Acknowledgments

This project builds on the research and open-source contributions of:

| Project | License | Reference |
| :--- | :---: | :--- |
| [Erlla/sony-a7m4-custom-firmware](https://github.com/erlla/sony-a7m4-custom-firmware) | `GPL-3.0` | Sony audio encoding, sample WAV sounds, and LUT research |
| [nolan2628/ZVE10-Astra-Public](https://github.com/nolan2628/ZVE10-Astra-Public) | `MIT` | ZV-E10 on-screen overlay rendering and input foundation |
| [dino2048/ZV-E10-5GHz-WiFi-Unlock](https://github.com/dino2048/ZV-E10-5GHz-WiFi-Unlock) | `MIT` | Gen 1 ZV-E10 native 5GHz Wi-Fi & channel unlock reference |
| [nolan2628/Sony-ZVE-10-Camtool](https://github.com/nolan2628/Sony-ZVE-10-Camtool) | `See repo` | ZV-E10 USB service-mode debugger, file staging, and reboot toolkit (`third_party/camtool` submodule) |
| [ma1co/Sony-PMCA-RE](https://github.com/ma1co/Sony-PMCA-RE) | `MIT` | Sony USB MSC/Senser authentication & transport library (`third_party/pmca` submodule) |
| [lvgl/lvgl](https://github.com/lvgl/lvgl) | `MIT` | Lightweight embedded UI library powering the overlay menu |

---

<div align="center">
Original code and docs are licensed under the <a href="LICENSE">MIT License</a> · Third-party components retain their respective licenses
</div>
