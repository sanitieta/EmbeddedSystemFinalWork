"""
天气数据与 MCU 输出助手 (E2)
===========================
- Open-Meteo 主数据源，wttr.in 备用数据源
- 尊重系统代理；遇到 ProxyError/SSLError 时自动尝试直连
- 严格校验 HTTP 状态和 JSON 结构
- 保留最近一次成功结果，短时网络故障时使用缓存
- USER2 将 ASCII 短消息和 LED5-LED7 编码下发到 MCU
"""

from __future__ import annotations

import time
import threading
from dataclasses import dataclass
from typing import Any, Callable

from PyQt5.QtCore import QObject, pyqtSignal

try:
    import requests
    _HAS_REQUESTS = True
except ImportError:
    requests = None
    _HAS_REQUESTS = False

from protocol import Protocol


OPEN_METEO_URL = "https://api.open-meteo.com/v1/forecast"
WTTR_URL = "https://wttr.in/Shanghai"
SHANGHAI_LATITUDE = 31.2304
SHANGHAI_LONGITUDE = 121.4737
REQUEST_TIMEOUT = (4, 6)  # connect, read
WEATHER_CACHE_MAX_AGE_S = 2 * 60 * 60
USER_AGENT = "S800-Device-Console/1.0"

MCU_CONDITION_CODES = {
    "SUNNY": "SUN",
    "CLOUDY": "CLD",
    "RAIN": "RAIN",
    "SNOW": "SNOW",
    "FOG": "FOG",
    "STORM": "STM",
    "WEATHER": "WX",
}


@dataclass(frozen=True)
class WeatherSnapshot:
    temperature_c: int
    condition: str
    description_zh: str
    provider: str

    @property
    def mcu_message(self) -> str:
        # USER2 是 8 位静态短显；完整单词会进入滚动模式，末尾字母容易
        # 被误认为残留字符。温度限制到两位，保证消息始终不超过 8 字符。
        temperature = max(-99, min(99, self.temperature_c))
        condition = MCU_CONDITION_CODES.get(self.condition, "WX")
        message = f"{temperature:d}C {condition}"
        if len(message) > 8:
            message = message.replace(" ", "", 1)
        return message[:8]

    @property
    def summary(self) -> str:
        return f"{self.temperature_c}°C {self.description_zh} · {self.provider}"


class WeatherProviderError(RuntimeError):
    """天气源请求或响应无效。"""


class WeatherHelper(QObject):
    """天气获取、缓存以及 MCU 消息/LED 编码。"""

    fetch_started = pyqtSignal(str)              # source: manual / timer / initial / USER2
    command_ready = pyqtSignal(str)              # 交给 MainWindow 统一记录并发送
    fetch_finished = pyqtSignal(bool, str, str)  # success, message, source

    def __init__(self, serial_worker, parent=None):
        super().__init__(parent)
        self.serial_worker = serial_worker
        self.protocol = Protocol()
        self._snapshot: WeatherSnapshot | None = None
        self._last_fetch_time: float = 0.0
        self._fetching = False
        self._send_after_fetch = False
        self._state_lock = threading.Lock()

        # 保留旧属性，兼容已有 UI/调试代码。
        self._temperature: int | None = None
        self._condition: str = ""

    @property
    def has_weather(self) -> bool:
        return self._snapshot is not None

    @property
    def fetching(self) -> bool:
        with self._state_lock:
            return self._fetching

    @property
    def snapshot(self) -> WeatherSnapshot | None:
        return self._snapshot

    def fetch_now(self) -> tuple[bool, str]:
        """同步兼容入口；GUI 应使用 request_fetch()。"""
        return self._do_fetch()

    def request_fetch(self, source: str = "manual", send_to_mcu: bool = False) -> bool:
        """启动后台刷新；正在刷新时合并 USER2 的发送请求。"""
        with self._state_lock:
            if self._fetching:
                self._send_after_fetch = self._send_after_fetch or send_to_mcu
                return False
            self._fetching = True
            self._send_after_fetch = send_to_mcu

        self.fetch_started.emit(source)
        worker = threading.Thread(
            target=self._run_fetch,
            args=(source,),
            name="s800-weather-fetch",
            daemon=True,
        )
        worker.start()
        return True

    def start_auto_refresh(self):
        """自动刷新由 MainWindow 的 QTimer 统一调度。"""

    def stop_auto_refresh(self):
        """保留兼容入口；当前 helper 不持有定时器。"""

    def send_weather_to_mcu(self) -> bool:
        """将最近天气以 7SEG 友好的 ASCII 消息和 LED5-LED7 编码下发。"""
        snapshot = self._snapshot
        if snapshot is None:
            return False

        self.command_ready.emit(
            self.protocol.build_set_msg(snapshot.mcu_message)
        )
        self.command_ready.emit(
            self.protocol.build_set_weather(self._compute_weather_led())
        )
        return True

    def send_status_led_to_mcu(self) -> bool:
        """串口重连后恢复最近天气的 LED5-LED7 编码 (不破坏 LED0-LED4)。"""
        if self._snapshot is None:
            return False
        self.command_ready.emit(
            self.protocol.build_set_weather(self._compute_weather_led())
        )
        return True

    def _run_fetch(self, source: str) -> None:
        success = False
        message = "天气刷新失败"
        try:
            success, message = self._do_fetch()
            with self._state_lock:
                send_after_fetch = self._send_after_fetch

            if success:
                if send_after_fetch:
                    self.send_weather_to_mcu()
                else:
                    self.send_status_led_to_mcu()
        except Exception as exc:
            success = False
            message = f"天气刷新异常: {self._short_error(exc)}"
        finally:
            with self._state_lock:
                self._fetching = False
                self._send_after_fetch = False
            self.fetch_finished.emit(success, message, source)

    def _do_fetch(self) -> tuple[bool, str]:
        if not _HAS_REQUESTS or requests is None:
            return False, "requests 库未安装"

        errors: list[str] = []
        providers: tuple[tuple[str, Callable[[], WeatherSnapshot]], ...] = (
            ("Open-Meteo", self._fetch_open_meteo),
            ("wttr.in", self._fetch_wttr),
        )

        for provider_name, fetcher in providers:
            try:
                snapshot = fetcher()
                self._apply_snapshot(snapshot)
                return True, snapshot.summary
            except WeatherProviderError as exc:
                errors.append(f"{provider_name}: {exc}")

        if self._cache_is_fresh():
            assert self._snapshot is not None
            return True, f"{self._snapshot.summary}（缓存）"

        return False, "天气源均不可用：" + "；".join(errors)

    def _fetch_open_meteo(self) -> WeatherSnapshot:
        data = self._request_json(
            OPEN_METEO_URL,
            params={
                "latitude": SHANGHAI_LATITUDE,
                "longitude": SHANGHAI_LONGITUDE,
                "current": "temperature_2m,weather_code",
                "timezone": "Asia/Shanghai",
            },
        )
        return self._parse_open_meteo(data)

    def _fetch_wttr(self) -> WeatherSnapshot:
        data = self._request_json(WTTR_URL, params={"format": "j1"})
        return self._parse_wttr(data)

    def _request_json(self, url: str, params: dict[str, Any]) -> dict[str, Any]:
        """先使用环境代理；代理/TLS握手失败时以相同证书校验直连。"""
        assert requests is not None
        attempt_errors: list[str] = []

        for trust_env in (True, False):
            session = requests.Session()
            session.trust_env = trust_env
            mode = "代理" if trust_env else "直连"
            try:
                response = session.get(
                    url,
                    params=params,
                    headers={"User-Agent": USER_AGENT, "Accept": "application/json"},
                    timeout=REQUEST_TIMEOUT,
                )
                response.raise_for_status()
                data = response.json()
                if not isinstance(data, dict):
                    raise ValueError("JSON 根节点不是对象")
                return data
            except Exception as exc:
                attempt_errors.append(f"{mode} {self._short_error(exc)}")
                is_proxy_or_tls = isinstance(
                    exc,
                    (requests.exceptions.ProxyError, requests.exceptions.SSLError),
                )
                if not trust_env or not is_proxy_or_tls:
                    break
            finally:
                session.close()

        raise WeatherProviderError(" / ".join(attempt_errors))

    @staticmethod
    def _parse_open_meteo(data: dict[str, Any]) -> WeatherSnapshot:
        current = data.get("current")
        if not isinstance(current, dict):
            raise WeatherProviderError("响应缺少 current")

        try:
            temperature = int(round(float(current["temperature_2m"])))
            weather_code = int(current["weather_code"])
        except (KeyError, TypeError, ValueError) as exc:
            raise WeatherProviderError(f"current 字段无效: {exc}") from exc

        condition, description = WeatherHelper._condition_from_wmo(weather_code)
        return WeatherSnapshot(temperature, condition, description, "Open-Meteo")

    @staticmethod
    def _parse_wttr(data: dict[str, Any]) -> WeatherSnapshot:
        rows = data.get("current_condition")
        if not isinstance(rows, list) or not rows or not isinstance(rows[0], dict):
            raise WeatherProviderError("响应缺少 current_condition")

        current = rows[0]
        try:
            temperature = int(round(float(current["temp_C"])))
        except (KeyError, TypeError, ValueError) as exc:
            raise WeatherProviderError(f"temp_C 字段无效: {exc}") from exc

        descriptions = current.get("weatherDesc")
        raw_description = "Unknown"
        if isinstance(descriptions, list) and descriptions:
            first = descriptions[0]
            if isinstance(first, dict):
                raw_description = str(first.get("value", "Unknown"))

        condition, description = WeatherHelper._condition_from_text(raw_description)
        return WeatherSnapshot(temperature, condition, description, "wttr.in")

    def _apply_snapshot(self, snapshot: WeatherSnapshot) -> None:
        self._snapshot = snapshot
        self._temperature = snapshot.temperature_c
        self._condition = snapshot.condition
        self._last_fetch_time = time.monotonic()

    def _cache_is_fresh(self) -> bool:
        return (
            self._snapshot is not None
            and time.monotonic() - self._last_fetch_time <= WEATHER_CACHE_MAX_AGE_S
        )

    def _compute_weather_led(self) -> int:
        """LED5=降水，LED6=高温，LED7=晴天。"""
        snapshot = self._snapshot
        if snapshot is None:
            return 0x00

        led = 0x00
        if snapshot.condition in {"RAIN", "SNOW", "STORM"}:
            led |= 0x20
        if snapshot.temperature_c > 35:
            led |= 0x40
        if snapshot.condition == "SUNNY":
            led |= 0x80
        return led

    @staticmethod
    def _condition_from_wmo(code: int) -> tuple[str, str]:
        if code == 0:
            return "SUNNY", "晴"
        if code in {1, 2, 3}:
            return "CLOUDY", "多云"
        if code in {45, 48}:
            return "FOG", "雾"
        if code in {51, 53, 55, 56, 57, 61, 63, 65, 66, 67, 80, 81, 82}:
            return "RAIN", "雨"
        if code in {71, 73, 75, 77, 85, 86}:
            return "SNOW", "雪"
        if code in {95, 96, 99}:
            return "STORM", "雷暴"
        return "WEATHER", "天气未知"

    @staticmethod
    def _condition_from_text(description: str) -> tuple[str, str]:
        value = description.lower()
        if any(word in value for word in ("thunder", "storm")):
            return "STORM", "雷暴"
        if any(word in value for word in ("rain", "drizzle", "shower")):
            return "RAIN", "雨"
        if any(word in value for word in ("snow", "sleet", "ice")):
            return "SNOW", "雪"
        if any(word in value for word in ("fog", "mist")):
            return "FOG", "雾"
        if any(word in value for word in ("sunny", "clear")):
            return "SUNNY", "晴"
        if any(word in value for word in ("cloud", "overcast")):
            return "CLOUDY", "多云"
        return "WEATHER", description.strip() or "天气未知"

    @staticmethod
    def _short_error(exc: Exception) -> str:
        text = " ".join(str(exc).split())
        if len(text) > 120:
            text = text[:117] + "..."
        return f"{type(exc).__name__}: {text}"
