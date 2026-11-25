# ZV-E10 自定义快门音示例资源

本目录收录了可直接复制到相机 SD 卡使用的测试 WAV 音频示例。

## SD 卡放置目录

将任意 `.WAV` 文件复制到 SD 卡以下目录之一（支持大小写 `.wav` / `.WAV`）：

- `/PRIVATE/M4ROOT/GENERAL/SOUNDS/`（推荐）
- `/PRIVATE/SONY/PRO/SOUNDS/`

插卡开机后进入插件菜单 **“自定义快门音”**，即可选择或通过拍摄界面的悬浮小窗快速切换与试听。

## WAV 格式支持说明

- 编码：未压缩 PCM (`audio_format = 1`)
- 采样率：`8000 Hz` ~ `192000 Hz`（插件内部会使用 Q24 Blackman-windowed Sinc 重采样为 24000 Hz Sony F0 数据块）
- 声道：单声道 (`1ch`) 或立体声 (`2ch`，自动混音为单声道)
- 位深：`8 / 16 / 24 / 32-bit`
- 体积与时长限制：单文件 ≤ `1 MiB`，时长 ≤ `5 秒`（建议快门音控制在 `0.1s ~ 1.5s`，过长会占用较多音频内存且影响连续点射体验）

## 示例文件清单

| 文件名 | 格式 / 采样率 | 时长 | 转码后 F0 体积 | 来源说明 |
| :--- | :--- | :--- | :--- | :--- |
| `DAGOU.WAV` | Stereo 16-bit PCM @ 44100 Hz | ~487.6 ms | ~12,096 B | `a7m4_cfw/examples/custom-sounds` 示例语音 |
| `JIAO.WAV` | Stereo 16-bit PCM @ 44100 Hz | ~487.6 ms | ~12,096 B | `a7m4_cfw/examples/custom-sounds` 示例语音 |
| `YEAH.WAV` | Stereo 16-bit PCM @ 44100 Hz | ~1114.6 ms | ~27,616 B | `a7m4_cfw/examples/custom-sounds` 示例语音 |
| `SHUTTER_MECH.WAV` | Mono 16-bit PCM @ 24000 Hz | ~145.0 ms | ~3,616 B | 无版权合成机械单反双帘快门音效（原生 24kHz 零重采样） |
| `SHUTTER_CHIME.WAV` | Mono 16-bit PCM @ 24000 Hz | ~180.0 ms | ~4,480 B | 无版权合成清脆电子双音快门音效（原生 24kHz 零重采样） |
