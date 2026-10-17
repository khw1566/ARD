# ARD ControlMCU (ESP-IDF 5.5)

ESP-IDF port of `ARD_software/ControlMCU` (Arduino/PlatformIO) for the ESP32-S3 N16R8 IPEX board:
3-position mode switch, latching voice button, 4-way joystick, WS2812 status LED, and the
MAX98357A speaker that plays everything the PC streams (TTS, beeps, radio, music, PC sound mirror).

## Build / flash

```powershell
.\build.ps1             # build
.\build.ps1 flash COM5  # build, flash, monitor
```

Run it from the PowerShell tool or a plain PowerShell window (Git Bash breaks `export.ps1`).

## Pins (unchanged from the Arduino build)

| Function | GPIO |
| :--- | :--- |
| Joystick up / down / left / right | 47 / 21 / 13 / 14 |
| Mode switch A / B (both high = N) | 35 / 36 |
| Voice button (latching) / its LED | 11 / 4 |
| WS2812B | 48 |
| I2S DOUT / WS / BCLK, amp SD | 40 / 41 / 42, 39 |

PSRAM is deliberately disabled: GPIO35/36 belong to the octal PSRAM bus on N16R8 modules.

## Protocol with the PC (ARD_MAIN)

| Direction | Port | Content |
| :--- | :--- | :--- |
| board -> PC | HTTP 5000 `POST /api/control/update` | `{mode, sub_mode, rssi, temp, joystick{up,down,left,right}}` on every change, every 120 ms while the joystick is held, else every 2 s |
| board -> PC | HTTP 5000 `POST /api/heartbeat` | every 2 s, `source:"ControlMCU"`, plus `firmware:"esp-idf"`, `volume`, `muted`, audio counters. No `status` key (the PC owns voice state) |
| board -> PC | HTTP 5000 `POST /api/controlmcu/voice/stop` | serial `s`/`x`/`q` (was `/api/soundmcu/stop`) |
| PC -> board | UDP 5005 | 16 kHz mono s16le PCM (1024-byte, 32 ms packets); packets under 20 bytes are `vol=N` / `VOL:N` |
| PC -> board | TCP 5002 | lines: `vol=N`, `volume=N`, `vol+`, `vol-`, `status`, `stop`, `mute`, `play`, `mic_ready`, `mic_done` |

Network settings use the shared `components/ard_net` (same NVS blob as the Arduino ArdNetwork, legacy
namespace `wifi-config` imported once). Setup AP `ard-control-xxxxxx`, page `/network`.

## Serial console (UART0, 115200)

`w` network setup, `u`/`d` volume, `v<0-21><Enter>` volume, `s`/`x`/`q` stop all audio (board and PC),
`m` mute, `p` unmute, `i` status, `?` help.

## Changes from the Arduino build

- On-board internet radio (ESP32-audioI2S, 10 hard-coded stations, keys `0`-`9`, `+`, `r`, TCP `radio=`)
  removed. The PC already decodes radio with ffmpeg and streams PCM over UDP 5005; `radio=` now answers
  with an error that points to the PC.
- HMI Ctrl tab `mute` / `play` now work (the old firmware ignored them). Mute also shuts the amplifier off.
- Jitter buffer (96 ms, `CONFIG_CONTROL_PREBUFFER_MS`) before playback, and silence on underrun instead
  of replaying the last DMA buffer.
- `stop` also discards packets still in flight for 500 ms so the speech does not resume for a moment.
- Volume is saved in NVS (old firmware returned to 12 after every reboot).
- Inputs are debounced (20 ms). Mode switch changes are sent immediately (old firmware waited up to 2 s).
- Sub mode follows the latching button level; old firmware reset it to 1 on a mode change even while the
  button stayed down.
- One HTTP client is reused; only `/api/control/update` is sent at the 120 ms jog rate (old firmware also
  sent a heartbeat every 120 ms).
- TCP 5002 closes a silent client after 5 s so it cannot block later commands.
- LED breathes red while Wi-Fi is down.

## Not verified yet

Built clean on 2026-10-17 with ESP-IDF 5.5.5; not flashed to the board yet.
