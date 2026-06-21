# CLAUDE.md - Current Firmware Development Guide

This repository is the MCU firmware project for the S800/TM4C1294 smart connected
clock assignment. Treat this file as the current executable engineering guide for
this codebase, not as a full restatement of every assignment document item.

## 1. Current Project Boundary

- Keep the existing modular firmware layout.
  - `User/main.c` owns only board initialization, app initialization, and the main
    loop (14 lines).
  - Application behavior lives in `User/app/` (alarm, boot, button, command,
    datetime, display, melody, app, app_state).
  - Reusable board and peripheral drivers live in `User/base/` (board, delay,
    hibernate_rtc, i2c_bus, pwm_buzzer, stepper, timebase, uart_port,
    board_config).
  - Interrupt callbacks remain in `User/callback.c`.
  - Shared type definitions and the global state struct live in
    `User/app/app_types.h` and `User/app/app_state.h`.
- Do not collapse custom MCU code into `main.c`.
- This stage implements MCU-side firmware only.
- Excluded for this stage:
  - no `pc_host/` implementation;
  - no Python, PyQt, serial worker, weather, NTP, analytics, or PC UI code;
  - no dynamic compilation or runtime code generation.

## 2. Build

```powershell
# Keil MDK-ARM v5 (ARM Compiler 5)
UV4.exe -r main.uvprojx -t "Target 1" -o build.log

# Or via the run skill:
powershell -NoProfile -ExecutionPolicy Bypass -File .claude/skills/run-main/driver.ps1 build
```

Current build: **0 errors, 0 warnings.**
Code=27546 RO-data=10206 RW-data=708 ZI-data=612 (Flash: ~38 KB / 1024 KB).

## 3. Module Map

### User/base/ — Hardware Abstraction Layer

| File | Purpose |
|------|---------|
| `board_config.h` | Centralized pin macros, timing constants, I2C addresses, command limits |
| `board.c/h` | Top-level `DevicesInit()`: clocks → UART → PWM → I2C → Hibernate → GPIO → Stepper → SysTick |
| `delay.c/h` | Blocking software delay loop |
| `hibernate_rtc.c/h` | Hibernate module init with RTC and low-drive oscillator |
| `i2c_bus.c/h` | I2C0 master: init TCA6424 + PCA9557, byte read/write |
| `pwm_buzzer.c/h` | PWM0 generator 3 on PK5: `PWMStart(freq_hz)` / `PWMStop()` |
| `stepper.c/h` | 28BYJ-48 stepper on PF0-PF3: 8-beat half-step sequence, ~1 RPM via Timer0A |
| `timebase.c/h` | `SysTickInit()` — configures 1 kHz SysTick. `SysTickUpdateTimebaseFlags()` exists but is **not called**; the actual ISR in `callback.c` does tick/flag management inline. |
| `uart_port.c/h` | UART0 115200-8N1 on PA0/PA1. Non-blocking string TX; RX ISR is in `callback.c`. |

### User/app/ — Application Layer

| File | Purpose |
|------|---------|
| `app_types.h` | All enums (`system_mode_t`, `setting_field_t`, `main_display_t`) and state structs |
| `app_state.c/h` | Single global `app_state_t g` with C99 designated initializers |
| `app.c/h` | `App_Init()` + `App_RunOnce()` main loop scheduler |
| `alarm.c/h` | Alarm match detection, 200ms ON/OFF buzzer cadence, 10s auto-stop, FUNC-silence |
| `boot_sequence.c/h` | Startup animation: all-on → student ID → name → version → melody start → normal |
| `button_logic.c/h` | 8-key matrix FSM: mode cycling, field increment/decrement, save with rollback |
| `command.c/h` | UART command parser: case-insensitive, space-tolerant, min-match tokens |
| `datetime.c/h` | `DateTime_TickOneSecond()` — carry-propagation for RTC catch-up during boot |
| `display.c/h` | 8-digit 7-seg scan (2ms), flow shift, format reverse, LED status, `*EVT:DISP` |
| `melody.c/h` | Non-blocking PWM melody player: "See You Again" (~380 notes, 23ms staccato gap) |

### Other

| File | Purpose |
|------|---------|
| `User/main.c` | `DevicesInit()` → `App_Init()` → `while(1) App_RunOnce()` |
| `User/callback.c` | `UART0_Handler`, `SysTick_Handler` (tick, flags, clock carry, button debounce, mode timeout), `TIMER0A_Handler` |

## 4. MCU Implementation Priorities

Implement and preserve these firmware behaviors in priority order:

1. Boot sequence:
   - all LEDs and all seven-segment digits flash;
   - show student ID `524031910772` (split across two frames);
   - show name as `XUHAORAN`;
   - show `v1.0`;
   - start melody playback ("See You Again") via PWM buzzer;
   - enter the normal clock display (FLOW mode).
2. Key handling:
   - debounce threshold is 20 ms (`DEBOUNCE_TIME_MS`);
   - long press threshold is 800 ms (`LONG_PRESS_TIME_MS`);
   - K3 (ADD) has 200 ms repeat on sustained long press;
   - mode timeout is 5 s (`MODE_TIMEOUT_MS`) — reverts to FLOW on inactivity;
   - keep the existing edit-state flow: date edit → time edit → alarm edit → normal.
3. UART command protocol:
   - parser remains case-insensitive and tolerant of extra spaces;
   - tokens support minimum-match (e.g. "HOU" matches "HOUR");
   - preserve current date, time, alarm, display, and format commands;
   - add the MCU-side commands listed below.
4. Message display:
   - `*SET:MSG <text>` accepts up to 32 bytes while preserving original casing;
   - messages of 8 characters or fewer display statically for 3 seconds;
   - longer messages scroll once (300 ms per shift), then return to the normal clock.
5. LED ownership:
   - default LED logic is firmware-owned;
   - `*SET:LED <hex2>` enters LED takeover mode and forces the hex pattern;
   - `*SET:LED 00` exits takeover mode and restores default LED logic.
6. Night Mode:
   - `*SET:MODE NIGHT` shows only HH.MM on four central digits, mutes the buzzer,
     and turns off status LEDs except heartbeat;
   - `*SET:MODE NORMAL` restores normal display, buzzer, and LED behavior.
7. Alarm:
   - trigger when hour, minute, and second exactly match the configured alarm;
   - sentinel value `alm_hh = 25` means alarm disabled;
   - buzzer cadence is 200 ms on / 200 ms off at 500 Hz;
   - ringing automatically stops after 10 seconds (`ALARM_RING_MAX_MS`);
   - pressing `FUNC` while ringing stops the alarm and must not enter edit mode;
   - `alarm_silenced` flag prevents re-trigger in the same matching second.
8. Melody:
   - plays "See You Again" piano chorus (~380 notes, B3–A6 range) after boot finishes;
   - non-blocking: `Melody_Update()` is called every main-loop iteration;
   - notes use 23 ms gap for staccato articulation.
9. Stepper motor:
   - runs continuously at ~1 RPM (4096 beats/rev, 8-beat half-step sequence);
   - controllable via `*MOTOR:START/STOP/FWD/REV` and `*GET:MOTOR`;
   - default direction is reverse, running after boot.

## 5. UART Protocol Reserved For PC Host

The PC host is reserved for later. The MCU must still expose the protocol surface
that the future host will consume.

- Button events:
  - `*EVT:KEY USER1` (FUNC key)
  - `*EVT:KEY USER2` (SET key)
- Display mirror event:
  - `*EVT:DISP <8 chars> <dpHex>`
  - `<8 chars>` is always exactly 8 display characters. Decimal points are not
    included in the characters.
  - `<dpHex>` is a two-digit uppercase hex bitmap. Bit N belongs to digit N.
- `FORMAT RIGHT` rules:
  - `*GET:` responses for full date/time/alarm display strings follow the active
    format;
  - `*EVT:DISP` reverses the 8-character visual string;
  - decimal-point bitmap is recomputed so separator dots remain attached to the
    correct visual digit. Example: left `12.30.45` uses `0A`; right `54.03.21`
    uses `28`.

## 6. UART Commands In Scope

- `*RST`
  - Restore protocol state to `DISP=ON`, `FORMAT=LEFT`, LED takeover off, and
    Night Mode off.
- `*SET:DATE ...`, `*SET:TIME ...`, `*SET:ALARM ...`
  - Keep the current partial-field command support.
- `*SET:DISPLAY ON/OFF`
- `*SET:FORMAT LEFT/RIGHT`
- `*SET:MSG <text>`
- `*SET:LED <hex2>`
- `*SET:MODE NIGHT/NORMAL`
- `*GET:DATE`, `*GET:TIME`, `*GET:ALARM`, `*GET:DISPLAY`, `*GET:FORMAT`
- `*MOTOR:START`, `*MOTOR:STOP`, `*MOTOR:FWD`, `*MOTOR:REV`
  - Stepper motor control: start/stop, set direction forward/reverse.
- `*GET:MOTOR`
  - Returns motor status (running/stopped, direction).
- `INIT`
  - Software system reset.
- `HELP`
  - Prints full command reference over UART.

## 7. Status LEDs

Default status LED map (written via PCA9557 I2C I/O expander):

- LED0: 1 Hz heartbeat.
- LED1: alarm enabled solid, ringing fast blink.
- LED2: edit mode active.
- LED3: UART RX/TX activity flash for about 100 ms.
- LED4-LED7: reserved for later PC/weather/NTP extensions and should remain off
  unless LED takeover is active.

Night Mode keeps only LED0 heartbeat. LED takeover overrides all default LED
logic until `*SET:LED 00` or `*RST`.

## 8. Key Map

| Key | Short Press | Long Press (≥800ms) |
|-----|------------|---------------------|
| K1 (FUNC) | Stop alarm; cycle mode: FLOW → DATE_SET → TIME_SET → ALARM_SET → ALARM_DISPLAY → FLOW | Same (advance to next mode) |
| K2 (SHIFT) | Cycle edit field: YEAR → MONTH → DAY → HOUR → MINUTE → SECOND → none | Same |
| K3 (ADD) | Increment active field (wraps) | Repeat at 200 ms intervals |
| K4 (SAVE) | Save edits (validates date); enable alarm if unset | Append-mode save; long-press timeout also saves |
| K5 (DISP) | Toggle display content: TIME → DATE → YEAR → FLOW | Same |
| K6 (SPEED) | Toggle flow speed: 500ms ↔ 300ms | Same |
| K7 (FORMAT) | Toggle direction: LEFT ↔ RIGHT | Same |
| K8 (EXT) | Send `*EVT:KEY EXT` over UART | N/A |

GPIO keys (PJ0/PJ1):
- USER1 (PJ0): sends `*EVT:KEY USER1` on short press.
- USER2 (PJ1): sends `*EVT:KEY USER2` on short press.

## 9. Design Notes

- **Clock increment lives in the SysTick ISR** (`callback.c`), not in
  `datetime.c`. The ISR increments `g.clock.ss` every 1s and performs
  carry propagation (ss→mm→hh→day→month→year) inline. `DateTime_TickOneSecond()`
  in `datetime.c` is only called during boot sequence to catch up time from the
  Hibernate RTC.
- **`timebase.c:SysTickUpdateTimebaseFlags()` is dead code** — not called from
  any ISR or main-loop path. The actual tick flag management is done inline in
  `callback.c:SysTick_Handler`.
- **`hibernate.h` fix**: TivaWare's `hibernate.h` uses `struct tm*` in function
  declarations without including `<time.h>`. This repo patches
  `driverlib/hibernate.h` line 59 with `#include <time.h>`. Do not revert.
- **Alarm sentinel**: `alm_hh = 25` (outside valid 0–23) means alarm is disabled.
  This sentinel is checked in alarm matching and display rendering.
- **Edit rollback**: Entering edit mode copies current values to `original_*`
  fields. On timeout (5s inactivity) or abandon, originals are restored.
- **`main.uvguix.xuhao`** is per-user Keil GUI layout — gitignored, never commit.

## 10. Verification Notes

- Do not add host-side files for this stage.
- Prefer static inspection or existing embedded build flows; do not add dynamic
  compile logic.
- Build command: `UV4.exe -r main.uvprojx -t "Target 1" -o build.log`
- Check UART behavior for:
  - `*RST` restoring display on, left format, normal mode, and default LED logic;
  - `*SET:FORMAT RIGHT` affecting `*GET:` strings and `*EVT:DISP`;
  - `*SET:MSG HI` displaying briefly and returning to clock;
  - long messages scrolling once and returning to clock;
  - `*SET:LED FF` forcing LEDs and `*SET:LED 00` restoring defaults;
  - `USER1` and `USER2` producing the corresponding `*EVT:KEY` lines.

## 11. 代码风格

- 确保模块化
- 不要改变原来的代码风格
- 新增模块遵循现有分层：`User/base/` 放硬件驱动，`User/app/` 放应用逻辑
