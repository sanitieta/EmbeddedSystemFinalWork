"""
自动昼夜模式 (E3)
==================
- 使用 astral 计算上海当天日出、日落时间
- 日出后自动下发 *SET:MODE DAY
- 日落后自动下发 *SET:MODE NIGHT
- 使用 Qt 定时器在 GUI 线程中调度，避免跨线程操作串口
"""

from datetime import date, datetime, timedelta, timezone
from typing import Optional

from PyQt5.QtCore import QObject, QTimer, pyqtSignal

try:
    from astral import LocationInfo
    from astral.sun import sun
    _HAS_ASTRAL = True
except ImportError:
    LocationInfo = None
    sun = None
    _HAS_ASTRAL = False

from protocol import Protocol


DEFAULT_LATITUDE = 31.23
DEFAULT_LONGITUDE = 121.47
DEFAULT_TIMEZONE = timezone(timedelta(hours=8), name="Asia/Shanghai")
CHECK_INTERVAL_MS = 30 * 1000


class AutoDayNight(QObject):
    """根据日出日落时间生成 DAY/NIGHT 命令。"""

    command_ready = pyqtSignal(str, str)       # command, target mode
    schedule_updated = pyqtSignal(str, str, str)  # target, sunrise, sunset
    enabled_changed = pyqtSignal(bool)
    error = pyqtSignal(str)

    def __init__(self, parent: Optional[QObject] = None):
        super().__init__(parent)
        self.protocol = Protocol()
        self._enabled = False
        self._target_mode = "DAY"
        self._device_mode: Optional[str] = None
        self._last_requested_mode: Optional[str] = None
        self._sunrise: Optional[datetime] = None
        self._sunset: Optional[datetime] = None
        self._schedule_date: Optional[date] = None
        self._reported_error = ""

        self._check_timer = QTimer(self)
        self._check_timer.setInterval(CHECK_INTERVAL_MS)
        self._check_timer.timeout.connect(self.refresh)

    @property
    def available(self) -> bool:
        return _HAS_ASTRAL

    @property
    def enabled(self) -> bool:
        return self._enabled

    @enabled.setter
    def enabled(self, value: bool):
        self.set_enabled(value)

    @property
    def current_mode(self) -> str:
        return self._target_mode

    @property
    def sunrise_time(self) -> Optional[datetime]:
        return self._sunrise

    @property
    def sunset_time(self) -> Optional[datetime]:
        return self._sunset

    def start(self, enabled: bool = True) -> bool:
        """启动周期检查，并立即计算一次当天计划。"""
        return self.set_enabled(enabled)

    def stop(self):
        self._check_timer.stop()

    def set_enabled(self, enabled: bool) -> bool:
        """启停自动模式；开启时立即请求当前应有的模式。"""
        enabled = bool(enabled)
        if enabled and not self.available:
            self._enabled = False
            self._check_timer.stop()
            self.enabled_changed.emit(False)
            self._emit_error("自动昼夜模式不可用：请安装 astral>=3.2")
            return False

        changed = enabled != self._enabled
        self._enabled = enabled
        if enabled:
            self._check_timer.start()
            self.refresh(force_command=changed)
        else:
            self._check_timer.stop()

        if changed:
            self.enabled_changed.emit(enabled)
        return True

    def calculate_today(self, local_date: Optional[date] = None):
        """计算指定日期的日出日落，返回带上海时区的 datetime。"""
        if not self.available or LocationInfo is None or sun is None:
            return None

        local_date = local_date or datetime.now(DEFAULT_TIMEZONE).date()
        try:
            location = LocationInfo(
                "Shanghai",
                "China",
                "Asia/Shanghai",
                DEFAULT_LATITUDE,
                DEFAULT_LONGITUDE,
            )
            values = sun(
                location.observer,
                date=local_date,
                tzinfo=DEFAULT_TIMEZONE,
            )
            self._sunrise = values["sunrise"]
            self._sunset = values["sunset"]
            self._schedule_date = local_date
            self._reported_error = ""
            return self._sunrise, self._sunset
        except Exception as exc:
            self._emit_error(f"计算日出日落失败: {exc}")
            return None

    @staticmethod
    def mode_for_time(now: datetime, sunrise: datetime, sunset: datetime) -> str:
        """返回给定时刻应处于的模式，日出（含）到日落前为 DAY。"""
        return "DAY" if sunrise <= now < sunset else "NIGHT"

    def refresh(self, force_command: bool = False) -> bool:
        """刷新计划并在目标模式变化时发出命令请求。"""
        if not self.available:
            self._emit_error("自动昼夜模式不可用：请安装 astral>=3.2")
            return False

        now = datetime.now(DEFAULT_TIMEZONE)
        if (
            self._schedule_date != now.date()
            or self._sunrise is None
            or self._sunset is None
        ):
            if self.calculate_today(now.date()) is None:
                return False

        target = self.mode_for_time(now, self._sunrise, self._sunset)
        self._target_mode = target
        self.schedule_updated.emit(
            target,
            self._sunrise.strftime("%H:%M"),
            self._sunset.strftime("%H:%M"),
        )

        needs_command = force_command or target != self._device_mode
        request_is_new = force_command or target != self._last_requested_mode
        if self._enabled and needs_command and request_is_new:
            self._last_requested_mode = target
            self.command_ready.emit(
                self.protocol.build_set_mode(night=(target == "NIGHT")),
                target,
            )
        return True

    def sync_now(self):
        """串口连接成功后强制同步一次当前目标模式。"""
        if self._enabled:
            self.refresh(force_command=True)

    def note_device_mode(self, mode: str):
        """记录 MCU 实际上报的 DAY/NIGHT 状态。"""
        mode = mode.upper()
        if mode not in ("DAY", "NIGHT"):
            return
        self._device_mode = mode
        if mode != self._target_mode:
            self._last_requested_mode = None

    def _emit_error(self, message: str):
        if message != self._reported_error:
            self._reported_error = message
            self.error.emit(message)
