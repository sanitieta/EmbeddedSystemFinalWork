"""
UART 协议解析器 (纯 Python，可单元测试)
========================================
负责解析 MCU 上报的所有 *EVT:* 事件，以及构建所有 *SET:/*GET:/*MOTOR: 命令。

要求: 零 Qt 依赖，可在命令行单独测试。
"""

from typing import Optional, Dict


class Protocol:
    """MCU ↔ PC 协议解析与构建"""

    # ---- Event Parsing ----

    def parse_event(self, line: str) -> Optional[Dict]:
        """
        解析一行 MCU 输出。
        如果是事件 (*EVT:*)，返回解析后的 dict。
        如果是普通应答/数据，返回 None。
        """
        if not line.startswith("*EVT:"):
            return None

        # 去掉 "*EVT:" 前缀
        payload = line[5:].strip()

        if payload.startswith("DISP "):
            return self._parse_disp(payload[5:])
        elif payload.startswith("LED "):
            return self._parse_led(payload[4:])
        elif payload.startswith("KEY "):
            return self._parse_key(payload[4:])
        elif payload.startswith("MODE "):
            return self._parse_mode(payload[5:])
        elif payload.startswith("ALARM OFF"):
            return {"type": "ALARM_OFF"}
        elif payload.startswith("ALARM"):
            return {"type": "ALARM"}
        elif payload.startswith("EDIT "):
            return self._parse_edit(payload[5:])
        else:
            # Unknown event type
            return {"type": "UNKNOWN", "raw": payload}

    def _parse_disp(self, args: str) -> Dict:
        """*EVT:DISP <8chars> <dpHex>

        MCU sends exactly 8 display characters, one space, then 2 hex digits.
        The 8-character payload may contain spaces (e.g. message display or
        alarm-disabled "AL xx xx"), so we use fixed-position extraction rather
        than whitespace splitting.
        """
        chars: list[str] = []
        dp: int = 0
        if len(args) >= 8:
            chars = list(args[:8])
        if len(args) >= 11:
            try:
                dp = int(args[9:11], 16) & 0xFF
            except ValueError:
                dp = 0
        return {"type": "DISP", "chars": chars, "dp": dp}

    def _parse_led(self, args: str) -> Dict:
        """*EVT:LED <hex2>"""
        try:
            pattern = int(args.strip(), 16)
        except ValueError:
            pattern = 0
        return {"type": "LED", "pattern": pattern}

    def _parse_key(self, args: str) -> Dict:
        """*EVT:KEY USER1 / USER2 / EXT"""
        return {"type": "KEY", "name": args.strip()}

    def _parse_mode(self, args: str) -> Dict:
        """*EVT:MODE FLOWING / DATE / TIME / ALARM / NIGHT / DAY"""
        return {"type": "MODE", "state": args.strip()}

    def _parse_edit(self, args: str) -> Dict:
        """*EVT:EDIT DATE yyyy.mm.dd / TIME hh.mm.ss / ALARM hh.mm.ss"""
        parts = args.split(maxsplit=1)
        field_type = parts[0] if parts else ""
        value = parts[1] if len(parts) > 1 else ""
        return {"type": "EDIT", "field": field_type, "value": value}

    def is_pong(self, line: str) -> bool:
        """检查是否 *PONG 应答"""
        return line.startswith("*PONG")

    # ---- Command Building ----

    @staticmethod
    def build_set_date(year=None, month=None, day=None) -> str:
        """构建 *SET:DATE 命令，支持部分字段"""
        parts = ["*SET:DATE"]
        vals = []
        if year is not None:
            parts.append("YEAR")
            vals.append(str(year))
        if month is not None:
            parts.append("MONTH")
            vals.append(str(month))
        if day is not None:
            parts.append("DATE")
            vals.append(str(day))
        if not vals:
            return "*SET:DATE"  # equivalent to *GET?
        return " ".join(parts + vals)

    @staticmethod
    def build_set_time(hour=None, minute=None, second=None) -> str:
        """构建 *SET:TIME 命令，支持部分字段"""
        parts = ["*SET:TIME"]
        vals = []
        if hour is not None:
            parts.append("HOUR")
            vals.append(str(hour))
        if minute is not None:
            parts.append("MINUTE")
            vals.append(str(minute))
        if second is not None:
            parts.append("SECOND")
            vals.append(str(second))
        if not vals:
            return "*SET:TIME"
        return " ".join(parts + vals)

    @staticmethod
    def build_set_alarm(hour=None, minute=None, second=None) -> str:
        """构建 *SET:ALARM 命令，支持部分字段"""
        parts = ["*SET:ALARM"]
        vals = []
        if hour is not None:
            parts.append("HOUR")
            vals.append(str(hour))
        if minute is not None:
            parts.append("MINUTE")
            vals.append(str(minute))
        if second is not None:
            parts.append("SECOND")
            vals.append(str(second))
        if not vals:
            return "*SET:ALARM"
        return " ".join(parts + vals)

    @staticmethod
    def build_set_display(on: bool) -> str:
        return "*SET:DISPLAY " + ("ON" if on else "OFF")

    @staticmethod
    def build_set_format(left: bool) -> str:
        return "*SET:FORMAT " + ("LEFT" if left else "RIGHT")

    @staticmethod
    def build_set_msg(text: str) -> str:
        # Max 32 bytes (not chars — but for ASCII they're equal)
        text = text[:32]
        return f"*SET:MSG {text}"

    @staticmethod
    def build_set_led(hex_byte: int) -> str:
        """hex_byte: 0x00 = restore default, 0x01-0xFF = takeover pattern"""
        return f"*SET:LED {hex_byte:02X}"

    @staticmethod
    def build_set_mode(night: bool) -> str:
        return "*SET:MODE " + ("NIGHT" if night else "DAY")

    @staticmethod
    def build_get_date(*fields) -> str:
        if fields:
            return "*GET:DATE " + " ".join(fields)
        return "*GET:DATE"

    @staticmethod
    def build_get_time(*fields) -> str:
        if fields:
            return "*GET:TIME " + " ".join(fields)
        return "*GET:TIME"

    @staticmethod
    def build_set_alarm_off() -> str:
        """关闭闹钟"""
        return "*SET:ALARM OFF"

    @staticmethod
    def build_get_alarm() -> str:
        return "*GET:ALARM"

    @staticmethod
    def build_get_display() -> str:
        return "*GET:DISPLAY"

    @staticmethod
    def build_get_format() -> str:
        return "*GET:FORMAT"

    @staticmethod
    def build_motor_start() -> str:
        return "*MOTOR:START"

    @staticmethod
    def build_motor_stop() -> str:
        return "*MOTOR:STOP"

    @staticmethod
    def build_motor_fwd() -> str:
        return "*MOTOR:FWD"

    @staticmethod
    def build_motor_rev() -> str:
        return "*MOTOR:REV"

    @staticmethod
    def build_get_motor() -> str:
        return "*GET:MOTOR"

    @staticmethod
    def build_rst() -> str:
        return "*RST"

    @staticmethod
    def build_init() -> str:
        return "INIT"

    @staticmethod
    def build_help() -> str:
        return "HELP"

    @staticmethod
    def build_set_key(name: str) -> str:
        """虚拟按键注入: USER1 / USER2 / EXT"""
        return f"*SET:KEY {name}"

    @staticmethod
    def build_set_weather(byte: int) -> str:
        """天气 LED5-LED7 指示 (不进入 LED 接管, 仅影响高 3 位)"""
        return f"*SET:WEATHER {byte & 0xE0:02X}"

    @staticmethod
    def build_set_beep(ms: int) -> str:
        """远程蜂鸣 (10-5000 ms, MCU 自动停止)"""
        return f"*SET:BEEP {max(10, min(5000, ms))}"

    @staticmethod
    def build_ntp_sync() -> str:
        return "*NTP SYNC"
