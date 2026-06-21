"""
天气获取助手 (E2)
==================
- 从 wttr.in 获取天气 (免费, 无 API Key)
- 启动时拉取一次, 每 30 分钟自动刷新
- USER2 触发: 下发 *SET:MSG 短显天气 5 秒
- LED 编码: 晴/雨/高温指示
"""

import time
import threading

try:
    import requests
    _HAS_REQUESTS = True
except ImportError:
    _HAS_REQUESTS = False

from protocol import Protocol


WEATHER_URL = "https://wttr.in/Shanghai?format=j1"
WEATHER_REFRESH_S = 30 * 60  # 30 分钟
REQUEST_TIMEOUT_S = 10


class WeatherHelper:
    """天气获取助手"""

    def __init__(self, serial_worker):
        self.serial_worker = serial_worker
        self.protocol = Protocol()
        self._temperature: int | None = None
        self._condition: str = ""
        self._last_fetch_time: float = 0.0

        # 后台定时刷新
        self._timer: threading.Timer | None = None

    def fetch_now(self) -> tuple[bool, str]:
        """立即获取天气，返回 (成功, 描述)"""
        return self._do_fetch()

    def start_auto_refresh(self):
        """启动 30 分钟自动刷新"""
        self._schedule_next()

    def stop_auto_refresh(self):
        """停止自动刷新"""
        if self._timer:
            self._timer.cancel()
            self._timer = None

    def send_weather_to_mcu(self):
        """
        USER2 触发 (E2): 下发天气消息到 MCU 数码管短显 5 秒
        由 main.py 在收到 *EVT:KEY USER2 时调用
        """
        if self._temperature is not None and self._condition:
            msg = f"{self._temperature}C {self._condition}"
        else:
            msg = "--C --"

        # 发送消息到 MCU (MCU 自动在 ≤8 字符时静显 2.5s, >8 字符时滚动)
        self.serial_worker.send_line(self.protocol.build_set_msg(msg))

        # 同时更新 LED 指示
        led = self._compute_weather_led()
        self.serial_worker.send_line(self.protocol.build_set_led(led))

    def _do_fetch(self) -> tuple[bool, str]:
        """实际 HTTP 请求"""
        if not _HAS_REQUESTS:
            return False, "requests 库未安装"

        try:
            resp = requests.get(WEATHER_URL, timeout=REQUEST_TIMEOUT_S)
            data = resp.json()

            current = data.get("current_condition", [{}])[0]
            self._temperature = int(current.get("temp_C", 0))
            self._condition = current.get("weatherDesc", [{}])[0].get("value", "Unknown")

            self._last_fetch_time = time.monotonic()

            # 更新 MCU LED 指示
            led = self._compute_weather_led()
            self.serial_worker.send_line(self.protocol.build_set_led(led))

            return True, f"{self._temperature}°C {self._condition}"
        except Exception as e:
            return False, f"天气请求失败: {e}"

    def _schedule_next(self):
        """安排下次 30 分钟刷新"""
        self._timer = threading.Timer(WEATHER_REFRESH_S, self._on_timer)
        self._timer.daemon = True
        self._timer.start()

    def _on_timer(self):
        self._do_fetch()
        self._schedule_next()

    def _compute_weather_led(self) -> int:
        """
        根据天气数据计算 LED 指示编码:
        - LED5 (bit 5): 雨天
        - LED6 (bit 6): 高温 (>35°C)
        - LED7 (bit 7): 晴天
        注: 这是 PC 侧建议编码, 需要通过 *SET:LED 下发到 MCU
        """
        led = 0x00
        if self._condition:
            cond_lower = self._condition.lower()
            if "rain" in cond_lower or "drizzle" in cond_lower or "shower" in cond_lower:
                led |= 0x20  # LED5
            if "sunny" in cond_lower or "clear" in cond_lower:
                led |= 0x80  # LED7
        if self._temperature is not None and self._temperature > 35:
            led |= 0x40  # LED6
        return led
