"""
S800/TM4C1294 智能互联时钟 — PC 上位机
==========================================
程序入口: python main.py

启动前请确认:
- MCU 已通过 USB-UART 连接到 PC
- COM 口未被其他程序占用
- requirements.txt 中的依赖已安装: pip install -r requirements.txt
"""

import sys
import os

# Ensure pc_host/ is on the path so we can import sibling modules
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QMessageBox, QSplitter,
    QStatusBar, QLabel, QToolBar, QComboBox, QPushButton,
    QWidget, QVBoxLayout,
)
from PyQt5.QtCore import Qt, QTimer

from serial_worker import SerialWorker
from protocol import Protocol
from twin_panel import TwinPanel
from control_panel import ControlPanel
from log_panel import LogPanel
from ntp_helper import NtpHelper
from weather_helper import WeatherHelper
from auto_daynight import AutoDayNight
from dashboard import Dashboard


class MainWindow(QMainWindow):
    """主窗口 — 组合所有面板并协调信号/槽连接"""

    def __init__(self):
        super().__init__()
        self.setWindowTitle("S800 智能互联时钟 — PC 上位机")
        self.resize(1280, 900)

        # ---- 状态缓存 ----
        self._current_format: str = "LEFT"
        self._alarm_time: str = ""

        # ---- 核心组件 ----
        self.serial_worker = SerialWorker()
        self.protocol = Protocol()

        # ---- UI 面板 ----
        self.twin_panel = TwinPanel(self)
        self.control_panel = ControlPanel(self)
        self.log_panel = LogPanel(self)

        # ---- 扩展功能模块 ----
        self.ntp_helper = NtpHelper(self.serial_worker)
        self.weather_helper = WeatherHelper(self.serial_worker)
        self.auto_daynight = AutoDayNight(self.serial_worker)
        self.dashboard = Dashboard()

        # ---- 构建 UI ----
        self._build_toolbar()
        self._build_central_widget()
        self._build_statusbar()

        # ---- 连接信号 ----
        self._connect_signals()

        # ---- 启动后台 ----
        self.serial_worker.start()

        # ---- 天气自动刷新定时器 (30 分钟) ----
        self._weather_timer = QTimer(self)
        self._weather_timer.timeout.connect(self._weather_fetch_callback)
        self._weather_timer.start(30 * 60 * 1000)

        # ---- 启动后首次拉取天气 (延迟 1s 等串口就绪) ----
        QTimer.singleShot(1000, self._initial_weather_fetch)

        # ---- 自动扫描 COM 口 ----
        self._scan_ports()

    # ═══════════════════════════════════════════════════════════════
    # UI 构建
    # ═══════════════════════════════════════════════════════════════

    def _build_toolbar(self):
        """构建顶部工具栏"""
        toolbar = QToolBar("主工具栏")
        toolbar.setMovable(False)
        self.addToolBar(toolbar)

        toolbar.addWidget(QLabel("COM: "))
        self.combo_com = QComboBox()
        self.combo_com.setMinimumWidth(130)
        self.combo_com.setToolTip("选择串口")
        toolbar.addWidget(self.combo_com)

        self.btn_refresh = QPushButton("刷新")
        self.btn_refresh.setToolTip("刷新 COM 口列表")
        toolbar.addWidget(self.btn_refresh)

        self.btn_connect = QPushButton("连接")
        self.btn_connect.setToolTip("连接/断开串口")
        toolbar.addWidget(self.btn_connect)

        toolbar.addSeparator()

        self.btn_ntp = QPushButton("NTP 对时")
        self.btn_ntp.setToolTip("从 NTP 服务器同步时间到 MCU")
        toolbar.addWidget(self.btn_ntp)

        self.btn_weather = QPushButton("刷新天气")
        self.btn_weather.setToolTip("立即从 wttr.in 获取天气")
        toolbar.addWidget(self.btn_weather)

        self.btn_dashboard = QPushButton("数据看板")
        self.btn_dashboard.setToolTip("查看事件统计图表")
        toolbar.addWidget(self.btn_dashboard)

    def _build_central_widget(self):
        """构建中央区域: QSplitter (TwinPanel | ControlPanel | LogPanel)"""
        splitter = QSplitter(Qt.Horizontal)

        self.twin_panel.setMinimumWidth(420)
        splitter.addWidget(self.twin_panel)

        splitter.addWidget(self.control_panel)

        splitter.addWidget(self.log_panel)

        # TwinPanel 初始宽度约 520px, 其余均分
        splitter.setSizes([520, 380, 380])
        splitter.setStretchFactor(0, 0)
        splitter.setStretchFactor(1, 1)
        splitter.setStretchFactor(2, 1)

        self.setCentralWidget(splitter)
        self._splitter = splitter

    def _build_statusbar(self):
        """构建状态栏: 5 个永久标签"""
        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)

        self.conn_label = QLabel("○ 未连接")
        self.conn_label.setStyleSheet("color: gray; font-weight: bold; padding: 0 8px;")
        self.status_bar.addPermanentWidget(self.conn_label)

        self.fmt_label = QLabel("FORMAT: LEFT")
        self.fmt_label.setStyleSheet("padding: 0 8px;")
        self.status_bar.addPermanentWidget(self.fmt_label)

        self.mode_label = QLabel("MODE: DAY")
        self.mode_label.setStyleSheet("padding: 0 8px;")
        self.status_bar.addPermanentWidget(self.mode_label)

        self.alarm_label = QLabel("ALARM: 未设置")
        self.alarm_label.setStyleSheet("padding: 0 8px;")
        self.status_bar.addPermanentWidget(self.alarm_label)

        self.ping_label = QLabel("PING: --ms")
        self.ping_label.setStyleSheet("padding: 0 8px;")
        self.status_bar.addPermanentWidget(self.ping_label)

        self.last_rx_label = QLabel("最后收到: --")
        self.last_rx_label.setStyleSheet("color: #666666; padding: 0 8px;")
        self.status_bar.addPermanentWidget(self.last_rx_label)

    # ═══════════════════════════════════════════════════════════════
    # 信号连接
    # ═══════════════════════════════════════════════════════════════

    def _connect_signals(self):
        """连接所有信号/槽"""
        sw = self.serial_worker

        # SerialWorker → 主窗口
        sw.line_received.connect(self._on_line_received)
        sw.connection_changed.connect(self._on_connection_changed)
        sw.latency_updated.connect(self._on_latency_updated)
        sw.port_error.connect(self._on_port_error)

        # ControlPanel / TwinPanel 发送命令 → 日志 + SerialWorker
        self.control_panel.send_command.connect(self._on_send_command)
        self.twin_panel.virtual_key_pressed.connect(self._on_send_command)

        # 工具栏按钮
        self.btn_refresh.clicked.connect(self._scan_ports)
        self.btn_connect.clicked.connect(self._on_connect_toggle)
        self.btn_ntp.clicked.connect(self._on_ntp_sync)
        self.btn_weather.clicked.connect(self._on_weather_fetch)
        self.btn_dashboard.clicked.connect(self._on_dashboard)

    # ═══════════════════════════════════════════════════════════════
    # COM 口扫描
    # ═══════════════════════════════════════════════════════════════

    def _scan_ports(self):
        """扫描可用 COM 口并填充下拉框, 优先选中 USB 串口"""
        self.combo_com.clear()
        ports = self.serial_worker.available_ports()

        if not ports:
            self.status_bar.showMessage("未检测到 COM 口 — 请检查 USB 连接", 5000)
            return

        self.combo_com.addItems(ports)

        # 优先选择 USB 串口设备 (CH340, CP210x, FTDI 等)
        usb_keywords = ["USB", "SERIAL", "UART", "CH340", "CP210", "FTDI", "STLINK", "DAPLINK"]
        for i, port in enumerate(ports):
            upper = port.upper()
            if any(kw in upper for kw in usb_keywords):
                self.combo_com.setCurrentIndex(i)
                return

        # 其次选择非 COM1 的端口 (COM1 通常是主板原生串口)
        for i, port in enumerate(ports):
            if port.upper() != "COM1":
                self.combo_com.setCurrentIndex(i)
                return

    # ═══════════════════════════════════════════════════════════════
    # 工具栏按钮处理
    # ═══════════════════════════════════════════════════════════════

    def _on_connect_toggle(self):
        """连接/断开按钮"""
        if self.serial_worker._connected:
            self.serial_worker.disconnect()
        else:
            port = self.combo_com.currentText()
            if not port:
                QMessageBox.warning(self, "提示", "请先选择 COM 口")
                return
            self.status_bar.showMessage("正在连接 " + port + " ...", 0)
            self.btn_connect.setEnabled(False)
            success = self.serial_worker.connect_to(port)
            self.btn_connect.setEnabled(True)
            if not success:
                self.status_bar.showMessage("连接 " + port + " 失败", 5000)
                QMessageBox.warning(self, "连接失败",
                                    f"无法打开 {port}\n请检查端口是否被其他程序占用。")
            else:
                self.status_bar.showMessage("已连接 " + port, 3000)
                # 立即验证 MCU 响应
                QTimer.singleShot(300, lambda: self.serial_worker.send_line("*GET:FORMAT"))
                QTimer.singleShot(450, lambda: self.serial_worker.send_line("*GET:ALARM"))

    def _on_ntp_sync(self):
        """手动触发 NTP 对时"""
        ok, msg = self.ntp_helper.sync_manual()
        if ok:
            self.status_bar.showMessage(f"NTP: {msg}", 5000)
            self.dashboard.log_event("SYNC", "NTP", msg)
        else:
            QMessageBox.warning(self, "NTP 对时失败", msg)
            self.log_panel.add_error(f"NTP: {msg}")

    def _on_weather_fetch(self):
        """手动刷新天气"""
        ok, msg = self.weather_helper.fetch_now()
        if ok:
            self.status_bar.showMessage(f"天气: {msg}", 5000)
        else:
            self.log_panel.add_error(f"天气: {msg}")

    def _weather_fetch_callback(self):
        """定时器回调：自动刷新天气"""
        ok, msg = self.weather_helper.fetch_now()
        if ok:
            self.status_bar.showMessage(f"天气自动更新: {msg}", 3000)

    def _initial_weather_fetch(self):
        """启动后首次拉取天气"""
        ok, msg = self.weather_helper.fetch_now()
        if ok:
            self.status_bar.showMessage(f"天气: {msg}", 5000)
        else:
            self.log_panel.add_error(f"天气: {msg}")

    def _on_dashboard(self):
        """打开数据看板"""
        self.status_bar.showMessage("数据看板 — 事件记录保存在 logs/events.csv", 3000)
        # 尝试嵌入 matplotlib 图表对话框
        self._show_dashboard_dialog()

    def _show_dashboard_dialog(self):
        """弹出数据看板对话框 (DashboardWindow with matplotlib charts)"""
        from dashboard import DashboardWindow

        # 确保至少有一条数据展示效果 (否则空图表让人困惑)
        events = self.dashboard.load_events()
        if not events:
            self.status_bar.showMessage("暂无事件数据 — 开始记录中...", 3000)

        win = DashboardWindow(self.dashboard, self)
        win.setWindowTitle("数据可视化看板 (E4)")
        win.setAttribute(Qt.WA_DeleteOnClose)
        win.show()

        # 保持引用防止过早回收
        if not hasattr(self, "_dashboard_windows"):
            self._dashboard_windows = []
        self._dashboard_windows.append(win)

    # ═══════════════════════════════════════════════════════════════
    # 命令发送处理
    # ═══════════════════════════════════════════════════════════════

    def _on_send_command(self, line: str):
        """命令发送: 记录 TX 日志 → 转发到串口线程"""
        self.log_panel.add_tx_command(line)
        self.serial_worker.send_line(line)

        # 跟踪 FORMAT 状态 (本地缓存)
        upper = line.upper()
        if "*SET:FORMAT" in upper:
            if "RIGHT" in upper:
                self._current_format = "RIGHT"
            elif "LEFT" in upper:
                self._current_format = "LEFT"
            self.fmt_label.setText(f"FORMAT: {self._current_format}")

        # *RST 复位协议状态
        if upper.startswith("*RST"):
            self._current_format = "LEFT"
            self.fmt_label.setText("FORMAT: LEFT")

    # ═══════════════════════════════════════════════════════════════
    # 接收行处理
    # ═══════════════════════════════════════════════════════════════

    def _on_line_received(self, line: str):
        """收到一行完整的 MCU 响应/事件"""
        from datetime import datetime
        now = datetime.now()
        self.last_rx_label.setText(
            f"最后收到: {now.strftime('%H:%M:%S')}"
        )
        event = self.protocol.parse_event(line)
        if event:
            self.log_panel.add_rx_event(line)
            self._dispatch_event(event)
        else:
            self.log_panel.add_rx_response(line)
            self._parse_response(line)

    def _dispatch_event(self, event: dict):
        """将解析后的事件分发到各面板和状态栏"""
        etype = event.get("type")

        if etype == "DISP":
            # 更新数字孪生显示
            self.twin_panel.update_display(event.get("chars", []), event.get("dp", 0))
            # 刷新 FORMAT 标签 (使用缓存的方向值)
            self.fmt_label.setText(f"FORMAT: {self._current_format}")

        elif etype == "LED":
            self.twin_panel.update_leds(event.get("pattern", 0))

        elif etype == "KEY":
            key_name = event.get("name", "")
            self.twin_panel.highlight_key(key_name)

            # 记录到 dashboard CSV
            self.dashboard.log_event("KEY", key_name)

            if key_name == "USER1":
                # E1: USER1 自动触发 NTP 对时
                self.status_bar.showMessage("收到 USER1，自动 NTP 对时中...", 2000)
                ok, msg = self.ntp_helper.sync_from_user1()
                if ok:
                    self.dashboard.log_event("SYNC", "NTP", msg)
                else:
                    self.log_panel.add_error(f"NTP: {msg}")

            elif key_name == "USER2":
                # E2: USER2 触发天气短显
                self.weather_helper.send_weather_to_mcu()

        elif etype == "MODE":
            state = event.get("state", "")
            self.twin_panel.update_mode(state)
            self._update_status_bar_mode(state)

        elif etype == "ALARM":
            self.alarm_label.setText("ALARM: RINGING")
            self.alarm_label.setStyleSheet("color: red; font-weight: bold; padding: 0 8px;")
            self.dashboard.log_event("ALARM")

        elif etype == "ALARM_OFF":
            # 恢复缓存的闹钟时间
            self.alarm_label.setStyleSheet("padding: 0 8px;")
            if self._alarm_time:
                self.alarm_label.setText(f"ALARM: {self._alarm_time}")
            else:
                self.alarm_label.setText("ALARM: 未设置")

        elif etype == "EDIT":
            # 记录到 dashboard CSV
            field = event.get("field", "")
            value = event.get("value", "")
            self.dashboard.log_event("EDIT", field, value)

        elif etype == "UNKNOWN":
            pass  # 未知事件类型，仅记录日志

    def _parse_response(self, line: str):
        """解析非事件响应行，更新状态栏和缓存"""
        upper = line.upper().strip()

        if upper.startswith("FORMAT:"):
            fmt = line.split(":", 1)[1].strip().upper()
            if fmt in ("LEFT", "RIGHT"):
                self._current_format = fmt
                self.fmt_label.setText(f"FORMAT: {fmt}")

        elif upper.startswith("ALARM:"):
            alarm_val = line.split(":", 1)[1].strip()
            # MCU 可能返回具体时间或 "--:--:--"/"DISABLED" 表示未设置
            if alarm_val and alarm_val not in ("--:--:--", "DISABLED", "OFF", ""):
                self._alarm_time = alarm_val
                self.alarm_label.setText(f"ALARM: {alarm_val}")
                self.alarm_label.setStyleSheet("padding: 0 8px;")
            else:
                self._alarm_time = ""
                self.alarm_label.setText("ALARM: 未设置")
                self.alarm_label.setStyleSheet("padding: 0 8px;")

        elif upper.startswith("MOTOR:"):
            self.status_bar.showMessage(f"电机: {line.strip()}", 3000)

        elif upper.startswith("DISPLAY:"):
            pass  # DISPLAY: ON/OFF 仅记录日志

        elif upper.startswith("DATE:") or upper.startswith("TIME:"):
            pass  # GET 响应仅记录日志

        elif upper.startswith("MODE:"):
            # 某些 MCU 实现可能在 GET 响应中返回 MODE 状态
            mode_val = line.split(":", 1)[1].strip()
            self._update_status_bar_mode(mode_val)

    # ═══════════════════════════════════════════════════════════════
    # 状态栏更新
    # ═══════════════════════════════════════════════════════════════

    def _update_status_bar_mode(self, state: str):
        """根据 MCU 上报的状态更新 MODE 标签"""
        upper = state.upper()
        mode_map = {
            "FLOWING": "FLOWING",
            "DATE": "EDIT",
            "TIME": "EDIT",
            "ALARM": "EDIT",
            "NIGHT": "NIGHT",
            "DAY": "DAY",
        }
        display_mode = mode_map.get(upper, upper)
        self.mode_label.setText(f"MODE: {display_mode}")

    def _on_connection_changed(self, connected: bool):
        """串口连接状态变更"""
        port = self.serial_worker.port_name
        if connected:
            self.conn_label.setText(f"● 已连接 {port}")
            self.conn_label.setStyleSheet("color: green; font-weight: bold; padding: 0 8px;")
            self.btn_connect.setText("断开")
        else:
            self.conn_label.setText("○ 未连接")
            self.conn_label.setStyleSheet("color: gray; font-weight: bold; padding: 0 8px;")
            self.btn_connect.setText("连接")
            self.ping_label.setText("PING: --ms")
            # 如果有端口错误，显示在状态栏和日志
            err = self.serial_worker.last_port_error
            if err:
                self.status_bar.showMessage("端口错误: " + err, 8000)
                self.log_panel.add_error("端口错误: " + err)

        # 启用/禁用控制面板控件
        self.control_panel.set_controls_enabled(connected)

    def _on_latency_updated(self, latency_ms: int):
        """PING/PONG 往返延迟更新"""
        self.ping_label.setText(f"PING: {latency_ms}ms")

    def _on_port_error(self, error_msg: str):
        """端口错误信号处理"""
        self.status_bar.showMessage("端口错误: " + error_msg, 8000)
        self.log_panel.add_error("端口错误: " + error_msg)

    # ═══════════════════════════════════════════════════════════════
    # 窗口关闭
    # ═══════════════════════════════════════════════════════════════

    def closeEvent(self, event):
        """窗口关闭时停止所有后台线程和定时器"""
        # 停止串口线程
        self.serial_worker.stop()
        self.serial_worker.wait(2000)

        # 停止天气自动刷新
        self._weather_timer.stop()
        self.weather_helper.stop_auto_refresh()

        # 停止自动昼夜模式定时器
        self.auto_daynight._cancel_timer()

        event.accept()


def main():
    app = QApplication(sys.argv)
    app.setStyle("Fusion")

    window = MainWindow()
    window.show()

    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
