"""
后台串口工作线程
================
负责: COM 口扫描、连接/断开、非阻塞收发、1Hz PING、心跳超时检测

关键:
- timeout=0.1 + ring buffer，绝不阻塞 GUI 线程
- 所有 UI 通信通过 pyqtSignal
- Windows: COM>9 需 \\\\.\\COMxx 格式
"""

from PyQt5.QtCore import QThread, pyqtSignal, QMutex, QMutexLocker
import serial
import serial.tools.list_ports
import time


class SerialWorker(QThread):
    """后台串口 I/O 线程"""

    # Signals (all cross-thread safe)
    line_received = pyqtSignal(str)        # 收到一个完整行 (已去换行符)
    connection_changed = pyqtSignal(bool)  # True=已连接, False=已断开
    latency_updated = pyqtSignal(int)      # 往返延迟 (ms)

    def __init__(self):
        super().__init__()
        self.serial: serial.Serial | None = None
        self._running = False
        self._rx_buffer = b""
        self._tx_mutex = QMutex()

        self.port_name: str = ""
        self.baudrate: int = 115200

        # 心跳
        self._last_ping_time: float = 0.0
        self._last_pong_time: float = 0.0
        self._ping_pending: bool = False
        self._ping_send_time: float = 0.0
        self._connected: bool = False

        # 常量
        self.PING_INTERVAL_S = 1.0
        self.PONG_TIMEOUT_S = 3.0

    # ---- Public API (called from main thread) ----

    def available_ports(self) -> list[str]:
        """扫描并返回可用 COM 口列表"""
        ports = serial.tools.list_ports.comports()
        result = [p.device for p in ports]
        # Windows COM>9 需要特殊前缀
        # pyserial 的 list_ports 通常已经返回了正确的名称
        return result

    def connect_to(self, port: str) -> bool:
        """连接到指定 COM 口"""
        try:
            self.serial = serial.Serial(
                port=port,
                baudrate=self.baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0.1,  # 非阻塞读取
                write_timeout=0.1,
            )
            self.port_name = port
            self._connected = True
            self._last_pong_time = time.monotonic()
            self.connection_changed.emit(True)
            return True
        except serial.SerialException as e:
            self._connected = False
            self.connection_changed.emit(False)
            return False

    def disconnect(self):
        """断开串口连接"""
        self._connected = False
        if self.serial and self.serial.is_open:
            self.serial.close()
        self.serial = None
        self.port_name = ""
        self.connection_changed.emit(False)

    def send_line(self, line: str):
        """发送一行命令 (自动追加 \\r\\n)"""
        if not self.serial or not self.serial.is_open:
            return
        data = (line.strip() + "\r\n").encode("ascii", errors="ignore")
        with QMutexLocker(self._tx_mutex):
            try:
                self.serial.write(data)
            except serial.SerialException:
                pass  # will be detected in run()

    def stop(self):
        """请求线程停止"""
        self._running = False

    # ---- Thread Core ----

    def run(self):
        """线程主循环 (在后台线程中运行)"""
        self._running = True
        self._last_ping_time = time.monotonic()

        while self._running:
            if self.serial and self.serial.is_open:
                try:
                    self._read_available()
                    self._check_heartbeat()
                except serial.SerialException:
                    self._handle_disconnect()
            else:
                pass  # 未连接时休眠
            self.msleep(50)  # ~20 Hz 循环

    def _read_available(self):
        """非阻塞读取所有可用字节，拆行后发射信号"""
        try:
            waiting = self.serial.in_waiting
            if waiting > 0:
                chunk = self.serial.read(waiting)
                self._rx_buffer += chunk

                # 拆行 (支持 \r\n 和 \n)
                while True:
                    # 查找行结束符
                    idx_crlf = self._rx_buffer.find(b"\r\n")
                    idx_lf = self._rx_buffer.find(b"\n")
                    idx = -1
                    split_len = 1
                    if idx_crlf >= 0 and (idx_lf < 0 or idx_crlf <= idx_lf):
                        idx = idx_crlf
                        split_len = 2
                    elif idx_lf >= 0:
                        idx = idx_lf
                        split_len = 1
                    else:
                        break

                    line_bytes = self._rx_buffer[:idx]
                    self._rx_buffer = self._rx_buffer[idx + split_len:]

                    line = line_bytes.decode("ascii", errors="ignore").strip()
                    if line:
                        self._process_line(line)

                # 防止缓冲区无限增长 (异常情况)
                if len(self._rx_buffer) > 4096:
                    self._rx_buffer = b""
        except serial.SerialException:
            raise

    def _process_line(self, line: str):
        """处理收到的一行文本"""
        # PONG 检测
        if line.startswith("*PONG"):
            self._ping_pending = False
            self._last_pong_time = time.monotonic()
            # 计算 RTT
            rtt_ms = int((time.monotonic() - self._ping_send_time) * 1000)
            self.latency_updated.emit(rtt_ms)

        # 统一对外发射
        self.line_received.emit(line)

    def _check_heartbeat(self):
        """1 Hz PING / 3s PONG 超时检测"""
        now = time.monotonic()

        # 发送 PING
        if now - self._last_ping_time >= self.PING_INTERVAL_S:
            self._last_ping_time = now
            self._ping_send_time = now
            self._ping_pending = True
            with QMutexLocker(self._tx_mutex):
                try:
                    self.serial.write(b"PING\r\n")
                except serial.SerialException:
                    pass

        # 检查 PONG 超时
        if self._connected and (now - self._last_pong_time > self.PONG_TIMEOUT_S):
            self._connected = False
            self.connection_changed.emit(False)

        # 恢复检测
        if not self._connected and (now - self._last_pong_time <= self.PONG_TIMEOUT_S):
            self._connected = True
            self.connection_changed.emit(True)

    def _handle_disconnect(self):
        """处理串口意外断开"""
        if self.serial:
            try:
                self.serial.close()
            except Exception:
                pass
        self.serial = None
        self._connected = False
        self._rx_buffer = b""
        self.connection_changed.emit(False)
