"""
NTP 时间同步助手 (E1)
=====================
- 从 NTP 服务器获取标准时间，转换为北京时间
- 手动按钮与 *EVT:KEY USER1 共用同一异步流程
- 成功后下发 *SET:DATE + *SET:TIME + *NTP SYNC
- 网络和解析异常只通过信号上报，不允许异常逃逸到 GUI 线程
"""

from __future__ import annotations

import threading
import time
from datetime import datetime, timedelta, timezone

from PyQt5.QtCore import QObject, pyqtSignal

try:
    import ntplib
    _HAS_NTPLIB = True
except ImportError:
    ntplib = None
    _HAS_NTPLIB = False

from protocol import Protocol


NTP_SERVERS = ("ntp.aliyun.com", "pool.ntp.org")
NTP_TIMEOUT_S = 5
COMMAND_INTERVAL_S = 0.12
BEIJING_TZ = timezone(timedelta(hours=8), name="UTC+8")


class NtpHelper(QObject):
    """后台执行 NTP 请求，并通过 Qt 信号回到主线程。"""

    sync_started = pyqtSignal(str)              # source: manual / USER1
    command_ready = pyqtSignal(str)             # 交给 MainWindow 统一记录并发送
    sync_finished = pyqtSignal(bool, str, str)  # success, message, source

    def __init__(self, serial_worker, parent=None):
        super().__init__(parent)
        self.serial_worker = serial_worker
        self.protocol = Protocol()
        self._last_sync_dt: datetime | None = None
        self._syncing = False
        self._state_lock = threading.Lock()

    @property
    def syncing(self) -> bool:
        with self._state_lock:
            return self._syncing

    def request_sync(self, source: str = "manual") -> bool:
        """启动一次异步同步；已有任务运行时返回 False。"""
        with self._state_lock:
            if self._syncing:
                return False
            self._syncing = True

        self.sync_started.emit(source)
        worker = threading.Thread(
            target=self._run_sync,
            args=(source,),
            name="s800-ntp-sync",
            daemon=True,
        )
        worker.start()
        return True

    def sync_manual(self) -> bool:
        """兼容入口：启动手动异步对时。"""
        return self.request_sync("manual")

    def sync_from_user1(self) -> bool:
        """兼容入口：启动 USER1 异步对时。"""
        return self.request_sync("USER1")

    def _run_sync(self, source: str) -> None:
        success = False
        message = "NTP 对时失败"
        commands: list[str] = []

        try:
            success, message, commands = self._perform_sync()
            if success:
                for index, command in enumerate(commands):
                    self.command_ready.emit(command)
                    if index + 1 < len(commands):
                        time.sleep(COMMAND_INTERVAL_S)
        except Exception as exc:
            # 最后一层异常边界：任何第三方库异常都不能导致上位机退出。
            success = False
            message = f"NTP 对时异常: {exc}"
        finally:
            with self._state_lock:
                self._syncing = False
            self.sync_finished.emit(success, message, source)

    def _perform_sync(self) -> tuple[bool, str, list[str]]:
        """执行网络请求和命令构建；该方法不触碰任何 QWidget。"""
        if not _HAS_NTPLIB or ntplib is None:
            return False, "ntplib 未安装", []

        response = None
        last_error: Exception | None = None
        for server in NTP_SERVERS:
            try:
                response = ntplib.NTPClient().request(
                    server,
                    version=3,
                    timeout=NTP_TIMEOUT_S,
                )
                break
            except Exception as exc:
                last_error = exc

        if response is None:
            return False, f"NTP 请求失败: {last_error}", []

        try:
            # ntplib 的 tx_time 已经是 Unix timestamp，不是 1900 epoch 秒数。
            beijing_time = datetime.fromtimestamp(
                float(response.tx_time),
                tz=timezone.utc,
            ).astimezone(BEIJING_TZ)
            offset_ms = int(round(float(response.offset) * 1000.0))
        except (AttributeError, OSError, OverflowError, TypeError, ValueError) as exc:
            return False, f"NTP 响应无效: {exc}", []

        self._last_sync_dt = beijing_time
        commands = [
            self.protocol.build_set_date(
                year=beijing_time.year,
                month=beijing_time.month,
                day=beijing_time.day,
            ),
            self.protocol.build_set_time(
                hour=beijing_time.hour,
                minute=beijing_time.minute,
                second=beijing_time.second,
            ),
            self.protocol.build_ntp_sync(),
        ]

        stamp = beijing_time.strftime("%Y-%m-%d %H:%M:%S")
        return True, f"同步成功 {stamp}（偏差 {offset_ms:+d} ms）", commands
