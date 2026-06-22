"""
自动昼夜模式 (E3)
==================
- 使用 astral 计算当地日出日落时间
- 日出 → *SET:MODE DAY
- 日落 → *SET:MODE NIGHT
- 夜间: 仅显示 4 位时分 + LED0 心跳 + 蜂鸣抑制
- UI: 复选框开关 + 显示当前模式 + 日出日落时刻
"""

import threading
import time
from datetime import datetime, timedelta

try:
    from astral import LocationInfo
    from astral.sun import sun
    _HAS_ASTRAL = True
except ImportError:
    _HAS_ASTRAL = False

from protocol import Protocol


# 默认位置: 上海
DEFAULT_LATITUDE = 31.23
DEFAULT_LONGITUDE = 121.47
DEFAULT_TIMEZONE = "Asia/Shanghai"


class AutoDayNight:
    """自动昼夜模式控制器"""

    def __init__(self, serial_worker):
        self.serial_worker = serial_worker
        self.protocol = Protocol()
        self._enabled: bool = True
        self._current_mode: str = "DAY"  # "DAY" or "NIGHT"
        self._sunrise: datetime | None = None
        self._sunset: datetime | None = None

        self._check_timer: threading.Timer | None = None

    # ── 属性 ──

    @property
    def enabled(self) -> bool:
        return self._enabled

    @enabled.setter
    def enabled(self, val: bool):
        self._enabled = val
        if val:
            self._recalculate_and_schedule()
        else:
            self._cancel_timer()

    @property
    def current_mode(self) -> str:
        return self._current_mode

    @property
    def sunrise_time(self) -> datetime | None:
        return self._sunrise

    @property
    def sunset_time(self) -> datetime | None:
        return self._sunset

    # ── 计算与调度 ──

    def calculate_today(self) -> tuple[datetime, datetime] | None:
        """计算今天的日出日落时间"""
        if not _HAS_ASTRAL:
            return None

        try:
            location = LocationInfo(
                "Shanghai", "China", DEFAULT_TIMEZONE,
                DEFAULT_LATITUDE, DEFAULT_LONGITUDE,
            )
            s = sun(location.observer, date=datetime.now().date())
            self._sunrise = s["sunrise"]
            self._sunset = s["sunset"]
            return self._sunrise, self._sunset
        except Exception:
            return None

    def _recalculate_and_schedule(self):
        """重新计算并安排下次检查"""
        result = self.calculate_today()
        if result is None:
            return

        sunrise, sunset = result
        self._apply_current_mode(sunrise, sunset)
        self._schedule_next_check(sunrise, sunset)

    def _apply_current_mode(self, sunrise: datetime, sunset: datetime):
        """根据当前时间确定应处于的模式并下发命令"""
        now = datetime.now()
        if sunrise <= now < sunset:
            target_mode = "DAY"
        else:
            target_mode = "NIGHT"

        if target_mode != self._current_mode:
            self._current_mode = target_mode
            is_night = (target_mode == "NIGHT")
            self.serial_worker.send_line(
                self.protocol.build_set_mode(night=is_night)
            )

    def _schedule_next_check(self, sunrise: datetime, sunset: datetime):
        """安排在下一个日出/日落时刻检查"""
        now = datetime.now()

        # 找到下一个事件 (日出或日落)
        next_events = []
        if sunrise > now:
            next_events.append(("sunrise", sunrise))
        if sunset > now:
            next_events.append(("sunset", sunset))

        if not next_events:
            # 今天的都过去了, 等明天的日出
            tomorrow = now.date() + timedelta(days=1)
            next_events.append(("sunrise", datetime.combine(tomorrow, sunrise.time())))

        next_events.sort(key=lambda x: x[1])
        next_event_time = next_events[0][1]

        delay = (next_event_time - now).total_seconds() + 1  # +1s 确保过了时刻
        delay = max(delay, 1.0)

        self._cancel_timer()
        self._check_timer = threading.Timer(delay, self._on_timer)
        self._check_timer.daemon = True
        self._check_timer.start()

    def _on_timer(self):
        """定时器回调: 重新计算并切换"""
        self._recalculate_and_schedule()

    def _cancel_timer(self):
        if self._check_timer:
            self._check_timer.cancel()
            self._check_timer = None
