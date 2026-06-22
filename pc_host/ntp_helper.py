"""
NTP 时间同步助手 (E1)
=====================
- 从 NTP 服务器获取标准时间
- 手动触发: 点击按钮
- 自动触发: 监听 *EVT:KEY USER1 后自动对时
- 对时完成后下发 *SET:DATE + *SET:TIME + *NTP SYNC
"""

import time
from datetime import datetime

# ntplib 为可选依赖，导入失败时降级
try:
    import ntplib
    _HAS_NTPLIB = True
except ImportError:
    _HAS_NTPLIB = False

from protocol import Protocol


NTP_SERVER = "ntp.aliyun.com"
NTP_FALLBACK = "pool.ntp.org"
NTP_TIMEOUT_S = 5


class NtpHelper:
    """NTP 对时助手"""

    def __init__(self, serial_worker):
        self.serial_worker = serial_worker
        self.protocol = Protocol()
        self._last_sync_dt: datetime | None = None

    def sync_manual(self) -> tuple[bool, str]:
        """手动触发 NTP 对时，返回 (成功, 消息)"""
        return self._do_sync()

    def sync_from_user1(self) -> tuple[bool, str]:
        """
        USER1 自动触发对时 (E1 要求)
        由 main.py 在收到 *EVT:KEY USER1 时调用
        返回 (成功, 消息)
        """
        return self._do_sync()

    def _do_sync(self) -> tuple[bool, str]:
        """执行 NTP 同步流程"""
        if not _HAS_NTPLIB:
            return False, "ntplib 未安装"

        try:
            client = ntplib.NTPClient()
            response = client.request(NTP_SERVER, timeout=NTP_TIMEOUT_S)
        except Exception:
            try:
                client = ntplib.NTPClient()
                response = client.request(NTP_FALLBACK, timeout=NTP_TIMEOUT_S)
            except Exception as e:
                return False, f"NTP 请求失败: {e}"

        # NTP 时间 = 1900-01-01 + seconds
        ntp_epoch = datetime(1900, 1, 1)
        utc_time = ntp_epoch + datetime.timedelta(seconds=response.tx_time)
        # 转为北京时间 (UTC+8)
        beijing_time = utc_time + datetime.timedelta(hours=8)

        self._last_sync_dt = beijing_time
        offset_ms = int(response.offset * 1000)

        # 下发到 MCU
        y, mo, d = beijing_time.year, beijing_time.month, beijing_time.day
        h, mi, s = beijing_time.hour, beijing_time.minute, beijing_time.second

        self.serial_worker.send_line(
            self.protocol.build_set_date(year=y, month=mo, day=d)
        )
        self.serial_worker.send_line(
            self.protocol.build_set_time(hour=h, minute=mi, second=s)
        )
        self.serial_worker.send_line(
            self.protocol.build_ntp_sync()
        )

        return True, f"NTP 对时成功 (±{offset_ms} ms)"
