---
name: run-main
description: Build, flash, and monitor the TM4C1294 smart connected clock firmware. Use for building firmware, flashing to hardware, serial UART interaction, verifying builds.
---

# TM4C1294 Smart Connected Clock — Run Skill

Embedded firmware for the S800/TM4C1294NCPDT smart connected clock. Cross-compiled with
Keil MDK-ARM v5 (ARM Compiler 5). The build produces `Objects/main.hex` for flash
download via Stellaris ICDI / CMSIS-DAP debug probe.

**Driver:** `.claude/skills/run-main/driver.ps1` — PowerShell build/test/flash harness.
All paths below are relative to the project root (where `main.uvprojx` lives).

## Prerequisites

- **Windows** with Keil MDK-ARM v5 installed at `D:\Programs\KEIL_V5\`
- ARM Compiler 5 (V5.06 update 5) — bundled with Keil
- **Hardware (for flash/monitor):** TM4C1294NCPDT board + Stellaris ICDI debug probe
- **Serial terminal (for UART interaction):** PuTTY, Tera Term, or any serial console

## Build

```powershell
# From project root:
powershell -NoProfile -ExecutionPolicy Bypass -File .claude/skills/run-main/driver.ps1 build
```

This invokes `UV4.exe -r main.uvprojx -t "Target 1"`, parses the build log, and:
- Reports error/warning counts (current: 0 errors, 0 warnings)
- Validates `Objects/main.hex` was generated
- Shows Flash/RAM usage vs. device limits (TM4C1294: 1024 KB Flash, 256 KB RAM)

### Build targets

| Action  | Description |
|---------|-------------|
| `build` | Compile firmware (default) |
| `clean` | Remove build artifacts (`Objects/*.o`, `*.hex`, `*.axf`, `build.log`) |
| `flash` | Download hex to TM4C1294 via debug probe |
| `monitor`| Print serial connection info and UART command reference |
| `all`   | Clean → build → flash |

## Run (agent path)

### 1. Build-only verification

```powershell
cd D:\xuhao\文档\Documents\大二下\嵌入式\大作业\Project
powershell -NoProfile -ExecutionPolicy Bypass -File .claude/skills/run-main/driver.ps1 build
```

Expected output ends with:
```
BUILD SUCCESS: 0 Error(s), 0 Warning(s)
Hex file: ...\Objects\main.hex (2367 lines, 106415 bytes)
Flash: 38460 bytes (max 1048576), RAM: 1320 bytes
```

### 2. Flash to hardware

Requires TM4C1294 board + debug probe connected:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .claude/skills/run-main/driver.ps1 flash
```

### 3. UART serial interaction

After flashing, connect serial terminal to UART0 (PA0=RX, PA1=TX, 115200 baud 8N1):

```
*GET:DATE           # Get current date
*GET:TIME           # Get current time
*GET:ALARM          # Get alarm settings
*GET:DISPLAY        # Get display state (ON/OFF)
*GET:FORMAT         # Get display format (LEFT/RIGHT)
*SET:DISPLAY ON     # Turn display on
*SET:DISPLAY OFF    # Turn display off
*SET:FORMAT LEFT    # Left-aligned display
*SET:FORMAT RIGHT   # Right-aligned display
*SET:MSG HELLO      # Display short message (≤8 chars: static; >8 chars: scroll once)
*SET:LED FF         # Take over LEDs (hex pattern)
*SET:LED 00         # Release LED takeover
*SET:MODE NIGHT     # Night mode: HH.MM only, buzzer muted, LEDs off except heartbeat
*SET:MODE NORMAL    # Restore normal mode
*SET:DATE 2026 6 21 # Set date
*SET:TIME 14 30 0   # Set time
*SET:ALARM 7 0 0    # Set alarm
*RST                # Reset protocol state (display on, left format, normal mode)
```

Button events emitted by the MCU:
```
*EVT:KEY USER1      # FUNC key pressed
*EVT:KEY USER2      # SET key pressed
*EVT:DISP <8chars> <dpHex>  # Display mirror (8 chars + decimal-point hex bitmap)
```

## Run (human path)

1. Open `main.uvprojx` in Keil µVision IDE
2. Click **Build** (F7) or **Rebuild**
3. Click **Download** (F8) to flash via debug probe
4. Open serial terminal (115200 baud) on the TM4C1294 UART0 COM port
5. Send UART commands terminated with CR+LF

## MCU Key Operation

| Key | Short press | Long press (≥800ms) |
|-----|------------|---------------------|
| FUNC (USER1) | Cycle: date→time→alarm→normal | Stop alarm (when ringing) |
| SET (USER2) | Enter/increment edit field | — |

Edit flow: normal → date edit (year/month/day) → time edit (hour/minute) → alarm edit (hour/minute) → normal.

## Gotchas

- **`struct tm` warnings:** TivaWare `hibernate.h` declares `HibernateCalendarSet(struct tm*)`
  without including `<time.h>`. This repo already patches `driverlib/hibernate.h` with
  `#include <time.h>` — don't revert that line.
- **Keil UV4 path:** Hardcoded to `D:\Programs\KEIL_V5\UV4\UV4.exe`. If Keil is installed
  elsewhere, update `$UV4` in `driver.ps1`.
- **Debug probe:** The project uses `UL2CM3.DLL` (CMSIS-DAP / Stellaris ICDI). Other
  debug probes (J-Link, ULINK) require changing the debug configuration in `main.uvprojx`.
- **Garbled characters:** The build log may show garbled CJK characters when run through
  bash. This is cosmetic — the error/warning counts are still parsed correctly.
- **`main.uvguix.xuhao`:** This is a per-user Keil GUI layout file. It's in `.gitignore`
  and should never be committed.
- **ARM Compiler 5 only:** The project uses ARMCC V5. ARM Compiler 6 (ARMCLANG) will
  fail due to different pragma/attribute syntax in TivaWare.

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `UV4.exe not found` | Install Keil MDK-ARM v5 or update `$UV4` path in driver.ps1 |
| `Flash Download failed` | Check debug probe USB connection; verify board is powered |
| `Error: Flash timeout` | The TM4C1294 may be locked. Use Keil IDE to unlock via debug menu |
| `No such file: melody.c` | A `melody.c` source was added to the project but not tracked in git. Add it or remove from `main.uvprojx` project file list |
| Serial output garbled | Verify baud rate is 115200 8N1; check UART0 pins (PA0/PA1) |
| `*RST` has no effect | Command requires CR+LF terminator (`\r\n`). Some terminals send only LF |
