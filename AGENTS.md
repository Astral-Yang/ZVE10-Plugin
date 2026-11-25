# AGENTS.md — AI Agent Guide for ZVE10-Plugin

Welcome, AI Agent. This repository provides an experimental `LD_PRELOAD` runtime plugin and LVGL v9.2 transparent overlay for **Sony ZV-E10 (Gen 1, `ILCE-ZV-E10`)**.

## Skills Routing

- **Installing, building, verifying, deploying, or uninstalling the plugin on a camera**:
  Read and strictly follow [`.agents/skills/zve10-plugin-install/SKILL.md`](.agents/skills/zve10-plugin-install/SKILL.md) before running any commands.

## Critical Guardrails

1. **Hardware Compatibility**: Only Gen 1 Sony ZV-E10 (`ILCE-ZV-E10`). Never deploy onto other camera models.
2. **Serial USB Execution**: Never run concurrent `cam.py` / `probe_zve10_runtime.py` USB commands.
3. **Mandatory ABI Verification**: Any newly built `build-arm/libzve10plugin.so` must pass `uv run --with pyelftools python tools/verify_elf_abi.py build-arm/libzve10plugin.so` before staging to the camera.
4. **Python Tooling**: Prefer `uv run` for all Python helper scripts.
