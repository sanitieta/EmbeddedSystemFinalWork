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
from collections import deque

# Ensure pc_host/ is on the path so we can import sibling modules
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QMessageBox, QSplitter,
    QStatusBar, QLabel, QToolBar, QComboBox, QPushButton,
    QWidget, QVBoxLayout, QHBoxLayout, QSizePolicy, QCheckBox,
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
        self.setObjectName("MainWindow")
        self.setWindowTitle("S800 Device Console")
        self.resize(1440, 920)
        self.setMinimumSize(1160, 740)

        # ---- 状态缓存 ----
        self._current_format: str = ""
        self._current_mode: str = ""
        self._alarm_enabled: bool | None = None
        self._alarm_time: str = ""
        self._pending_responses: deque[str] = deque()
        self._dashboard_window: QWidget | None = None

        # ---- 核心组件 ----
        self.serial_worker = SerialWorker()
        self.protocol = Protocol()

        # ---- UI 面板 ----
        self.twin_panel = TwinPanel(self)
        self.control_panel = ControlPanel(self)
        self.log_panel = LogPanel(self)

        # ---- 扩展功能模块 ----
        self.ntp_helper = NtpHelper(self.serial_worker, self)
        self.weather_helper = WeatherHelper(self.serial_worker, self)
        self.auto_daynight = AutoDayNight(self)
        self.dashboard = Dashboard()

        # ---- 构建 UI ----
        self._build_toolbar()
        self._build_central_widget()
        self._build_statusbar()

        # ---- 加载主题 (在 UI 构建完成后) ----
        self._load_theme()

        # ---- 连接信号 ----
        self._connect_signals()

        # ---- 自动昼夜模式（默认开启） ----
        self.auto_daynight.start(enabled=True)

        # ---- 启动后台 ----
        self.serial_worker.start()

        # ---- 天气自动刷新定时器 (30 分钟) ----
        self._weather_timer = QTimer(self)
        self._weather_timer.timeout.connect(self._weather_fetch_callback)
        self._weather_timer.start(30 * 60 * 1000)

        # ---- 设备状态轮询：补足 FORMAT 无主动事件的问题 ----
        self._status_poll_timer = QTimer(self)
        self._status_poll_timer.setInterval(2000)
        self._status_poll_timer.timeout.connect(self._poll_device_status)
        self._status_poll_timer.start()

        # ---- 启动后首次拉取天气 (延迟 1s 等串口就绪) ----
        QTimer.singleShot(1000, self._initial_weather_fetch)

        # ---- 自动扫描 COM 口 ----
        self._scan_ports()

    # ═══════════════════════════════════════════════════════════════
    # UI 构建
    # ═══════════════════════════════════════════════════════════════

    def _build_toolbar(self):
        """构建顶部工具栏"""
        toolbar = QToolBar("Application Header")
        toolbar.setObjectName("appHeader")
        toolbar.setMovable(False)
        toolbar.setFloatable(False)
        self.addToolBar(toolbar)

        brand = QWidget()
        brand.setObjectName("brandBlock")
        brand_layout = QVBoxLayout(brand)
        brand_layout.setContentsMargins(0, 0, 22, 0)
        brand_layout.setSpacing(0)
        brand_title = QLabel("S800")
        brand_title.setObjectName("brandTitle")
        brand_subtitle = QLabel("DEVICE CONSOLE  /  TM4C1294")
        brand_subtitle.setObjectName("brandSubtitle")
        brand_layout.addWidget(brand_title)
        brand_layout.addWidget(brand_subtitle)
        toolbar.addWidget(brand)

        port_block = QWidget()
        port_block.setObjectName("portBlock")
        port_layout = QVBoxLayout(port_block)
        port_layout.setContentsMargins(16, 0, 0, 0)
        port_layout.setSpacing(3)
        port_caption = QLabel("SERIAL PORT")
        port_caption.setObjectName("toolbarCaption")
        port_row = QHBoxLayout()
        port_row.setContentsMargins(0, 0, 0, 0)
        port_row.setSpacing(6)
        self.combo_com = QComboBox()
        self.combo_com.setMinimumWidth(150)
        self.combo_com.setToolTip("选择串口")
        port_row.addWidget(self.combo_com)

        self.btn_refresh = QPushButton("↻")
        self.btn_refresh.setObjectName("refreshButton")
        self.btn_refresh.setToolTip("刷新 COM 口列表")
        port_row.addWidget(self.btn_refresh)

        self.btn_connect = QPushButton("连接")
        self.btn_connect.setObjectName("connectButton")
        self.btn_connect.setProperty("primary", True)
        self.btn_connect.setMinimumWidth(88)
        self.btn_connect.setToolTip("连接/断开串口")
        port_row.addWidget(self.btn_connect)
        port_layout.addWidget(port_caption)
        port_layout.addLayout(port_row)
        toolbar.addWidget(port_block)

        spacer = QWidget()
        spacer.setObjectName("toolbarSpacer")
        spacer.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
        toolbar.addWidget(spacer)

        quick_block = QWidget()
        quick_block.setObjectName("quickActions")
        quick_layout = QVBoxLayout(quick_block)
        quick_layout.setContentsMargins(0, 0, 0, 0)
        quick_layout.setSpacing(3)
        quick_caption = QLabel("QUICK ACTIONS")
        quick_caption.setObjectName("toolbarCaption")
        quick_row = QHBoxLayout()
        quick_row.setContentsMargins(0, 0, 0, 0)
        quick_row.setSpacing(6)

        self.btn_ntp = QPushButton("◷  NTP 对时")
        self.btn_ntp.setProperty("toolbarAction", True)
        self.btn_ntp.setToolTip("从 NTP 服务器同步时间到 MCU")
        quick_row.addWidget(self.btn_ntp)

        self.btn_weather = QPushButton("☁  天气")
        self.btn_weather.setProperty("toolbarAction", True)
        self.btn_weather.setToolTip("刷新上海天气（Open-Meteo / wttr.in 备用）")
        quick_row.addWidget(self.btn_weather)

        self.btn_dashboard = QPushButton("⌁  数据看板")
        self.btn_dashboard.setProperty("toolbarAction", True)
        self.btn_dashboard.setToolTip("查看事件统计图表")
        quick_row.addWidget(self.btn_dashboard)
        quick_layout.addWidget(quick_caption)
        quick_layout.addLayout(quick_row)
        toolbar.addWidget(quick_block)

    def _build_central_widget(self):
        """构建中央区域: 设备孪生 | (控制面板 / UART 终端)。"""
        shell = QWidget()
        shell.setObjectName("workspaceShell")
        shell_layout = QVBoxLayout(shell)
        shell_layout.setContentsMargins(16, 16, 16, 14)
        shell_layout.setSpacing(10)

        heading_row = QHBoxLayout()
        heading = QVBoxLayout()
        heading.setSpacing(1)
        workspace_title = QLabel("设备工作台")
        workspace_title.setObjectName("workspaceTitle")
        workspace_subtitle = QLabel("实时镜像、设备控制与 UART 遥测")
        workspace_subtitle.setObjectName("workspaceSubtitle")
        heading.addWidget(workspace_title)
        heading.addWidget(workspace_subtitle)
        heading_row.addLayout(heading)
        heading_row.addStretch()

        daynight_summary = QWidget()
        daynight_summary.setObjectName("dayNightSummary")
        daynight_layout = QHBoxLayout(daynight_summary)
        daynight_layout.setContentsMargins(0, 0, 8, 0)
        daynight_layout.setSpacing(8)
        self.chk_auto_daynight = QCheckBox("自动昼夜")
        self.chk_auto_daynight.setObjectName("autoDayNightToggle")
        self.chk_auto_daynight.setChecked(True)
        self.chk_auto_daynight.setToolTip("按上海日出日落时间自动切换 DAY / NIGHT")
        daynight_layout.addWidget(self.chk_auto_daynight)
        self.sun_schedule_label = QLabel("☀ --:--  ☾ --:--")
        self.sun_schedule_label.setObjectName("sunScheduleLabel")
        self.sun_schedule_label.setToolTip("上海当天日出 / 日落时间")
        daynight_layout.addWidget(self.sun_schedule_label)
        heading_row.addWidget(daynight_summary, alignment=Qt.AlignVCenter)

        protocol_badge = QLabel("UART  ·  115200 8N1")
        protocol_badge.setObjectName("protocolBadge")
        heading_row.addWidget(protocol_badge, alignment=Qt.AlignVCenter)
        shell_layout.addLayout(heading_row)

        splitter = QSplitter(Qt.Horizontal)
        splitter.setObjectName("workspaceSplitter")
        splitter.setChildrenCollapsible(False)

        self.twin_panel.setMinimumWidth(540)
        splitter.addWidget(self.twin_panel)

        operations = QSplitter(Qt.Vertical)
        operations.setObjectName("operationsSplitter")
        operations.setChildrenCollapsible(False)
        self.control_panel.setMinimumHeight(330)
        self.log_panel.setMinimumHeight(190)
        operations.addWidget(self.control_panel)
        operations.addWidget(self.log_panel)
        operations.setSizes([520, 260])
        operations.setStretchFactor(0, 2)
        operations.setStretchFactor(1, 1)
        splitter.addWidget(operations)

        splitter.setSizes([590, 790])
        splitter.setStretchFactor(0, 4)
        splitter.setStretchFactor(1, 6)

        shell_layout.addWidget(splitter)
        self.setCentralWidget(shell)
        self._splitter = splitter
        self._operations_splitter = operations

    def _build_statusbar(self):
        """构建状态栏：连接、FORMAT、MODE、ALARM 使能四项。"""
        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)

        self.conn_label = QLabel("●  OFFLINE")
        self.conn_label.setProperty("statusChip", True)
        self.conn_label.setProperty("state", "offline")
        self.status_bar.addPermanentWidget(self.conn_label)

        self.fmt_label = QLabel("FORMAT: --")
        self.fmt_label.setProperty("statusChip", True)
        self.fmt_label.setProperty("state", "unknown")
        self.status_bar.addPermanentWidget(self.fmt_label)

        self.mode_label = QLabel("MODE: --")
        self.mode_label.setProperty("statusChip", True)
        self.mode_label.setProperty("state", "unknown")
        self.status_bar.addPermanentWidget(self.mode_label)

        self.alarm_label = QLabel("ALARM: --")
        self.alarm_label.setProperty("statusChip", True)
        self.alarm_label.setProperty("state", "unknown")
        self.status_bar.addPermanentWidget(self.alarm_label)

    @staticmethod
    def _set_widget_state(widget: QWidget, state: str) -> None:
        """更新动态属性，并让 QSS 立即重新匹配选择器。"""
        widget.setProperty("state", state)
        widget.style().unpolish(widget)
        widget.style().polish(widget)
        widget.update()

    def _load_theme(self):
        """加载 pc_host/theme.qss 并应用到全局样式表。如果文件缺失则记录警告并继续。"""
        theme_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "theme.qss")
        if os.path.exists(theme_path):
            try:
                with open(theme_path, "r", encoding="utf-8") as f:
                    qss = f.read()
                QApplication.instance().setStyleSheet(qss)
            except OSError as e:
                self.log_panel.add_error(f"加载 theme.qss 失败: {e}")
        else:
            self.log_panel.add_error("theme.qss 未找到，使用默认 Fusion 样式")

    # ═══════════════════════════════════════════════════════════════
    # 信号连接
    # ═══════════════════════════════════════════════════════════════

    def _connect_signals(self):
        """连接所有信号/槽"""
        sw = self.serial_worker

        # SerialWorker → 主窗口
        sw.line_received.connect(self._on_line_received)
        sw.connection_changed.connect(self._on_connection_changed)
        sw.heartbeat_sent.connect(self.log_panel.add_tx_command)
        sw.port_error.connect(self._on_port_error)

        # ControlPanel / TwinPanel 发送命令 → 日志 + SerialWorker
        self.control_panel.send_command.connect(self._on_send_command)
        self.twin_panel.virtual_key_pressed.connect(self._on_send_command)
        self.ntp_helper.command_ready.connect(self._on_send_command)
        self.ntp_helper.sync_started.connect(self._on_ntp_sync_started)
        self.ntp_helper.sync_finished.connect(self._on_ntp_sync_finished)
        self.weather_helper.command_ready.connect(self._on_weather_command)
        self.weather_helper.fetch_started.connect(self._on_weather_fetch_started)
        self.weather_helper.fetch_finished.connect(self._on_weather_fetch_finished)
        self.auto_daynight.command_ready.connect(self._on_auto_daynight_command)
        self.auto_daynight.schedule_updated.connect(self._on_daynight_schedule_updated)
        self.auto_daynight.enabled_changed.connect(self._on_auto_daynight_enabled_changed)
        self.auto_daynight.error.connect(self._on_auto_daynight_error)

        # 工具栏按钮
        self.btn_refresh.clicked.connect(self._scan_ports)
        self.btn_connect.clicked.connect(self._on_connect_toggle)
        self.btn_ntp.clicked.connect(self._on_ntp_sync)
        self.btn_weather.clicked.connect(self._on_weather_fetch)
        self.btn_dashboard.clicked.connect(self._on_dashboard)
        self.chk_auto_daynight.toggled.connect(self._on_auto_daynight_toggled)

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
            try:
                success = self.serial_worker.connect_to(port)
            except OSError as e:
                success = False
                self.serial_worker.last_port_error = str(e)
                self.serial_worker.port_error.emit(str(e))
            self.btn_connect.setEnabled(True)
            if not success:
                err = self.serial_worker.last_port_error
                if err and ("PermissionError" in err or "拒绝访问" in err):
                    self.status_bar.showMessage("连接 " + port + " 失败 — COM口被占用", 5000)
                    QMessageBox.warning(self, "连接失败",
                                        f"COM口被占用\n\n{port} 正在被另一个程序使用。\n请关闭其他串口工具（如 SecureCRT、Putty、串口助手等）后重试。")
                else:
                    self.status_bar.showMessage("连接 " + port + " 失败", 5000)
                    QMessageBox.warning(self, "连接失败",
                                        f"无法打开 {port}\n请检查端口是否被其他程序占用。")
            else:
                self.status_bar.showMessage("已连接 " + port, 3000)
                # 连接状态信号中会立即拉取设备状态。

    def _on_ntp_sync(self):
        """手动按钮触发 NTP 对时。"""
        self._request_ntp_sync("manual")

    def _request_ntp_sync(self, source: str):
        """按钮和 USER1 共用的唯一 NTP 对时入口。"""
        if source == "manual" and not self.serial_worker._connected:
            msg = "请先连接 S800 板，再执行 NTP 对时"
            self.status_bar.showMessage(msg, 5000)
            QMessageBox.warning(self, "NTP 对时", msg)
            return

        if not self.ntp_helper.request_sync(source):
            self.status_bar.showMessage("NTP 对时正在进行，请稍候", 3000)

    def _on_ntp_sync_started(self, source: str):
        """NTP 后台任务开始。"""
        self.btn_ntp.setEnabled(False)
        self.btn_ntp.setText("◷  对时中…")
        trigger = "USER1" if source == "USER1" else "手动"
        self.status_bar.showMessage(f"{trigger}触发 NTP 对时，正在获取标准时间…", 0)

    def _on_ntp_sync_finished(
        self, ok: bool, msg: str, source: str, offset_ms
    ):
        """NTP 后台任务完成；该槽始终在 GUI 线程执行。"""
        self.btn_ntp.setEnabled(True)
        self.btn_ntp.setText("◷  NTP 对时")
        if ok:
            self.status_bar.showMessage(f"NTP: {msg}", 5000)
            if offset_ms is not None:
                self.dashboard.log_event("SYNC", "NTP", str(offset_ms))
                if self._dashboard_window is not None:
                    self._dashboard_window.refresh_now()
        else:
            self.status_bar.showMessage(f"NTP: {msg}", 8000)
            self.log_panel.add_error(f"NTP: {msg}")
            if source == "manual":
                QMessageBox.warning(self, "NTP 对时失败", msg)

    def _on_weather_fetch(self):
        """手动刷新天气。"""
        self._request_weather_fetch("manual")

    def _request_weather_fetch(self, source: str, send_to_mcu: bool = False):
        """启动异步天气刷新，合并重复请求。"""
        if not self.weather_helper.request_fetch(source, send_to_mcu):
            self.status_bar.showMessage("天气正在更新，请稍候", 3000)

    def _on_weather_fetch_started(self, source: str):
        """天气后台任务开始。"""
        self.btn_weather.setEnabled(False)
        self.btn_weather.setText("☁  更新中…")
        if source in ("manual", "USER2"):
            self.status_bar.showMessage("正在获取上海实时天气…", 0)

    def _on_weather_fetch_finished(self, ok: bool, msg: str, source: str):
        """天气后台任务完成。"""
        self.btn_weather.setEnabled(True)
        self.btn_weather.setText("☁  天气")
        if ok:
            prefix = "天气自动更新" if source == "timer" else "天气"
            self.status_bar.showMessage(f"{prefix}: {msg}", 5000)
        else:
            self.status_bar.showMessage(f"天气: {msg}", 8000)
            self.log_panel.add_error(f"天气: {msg}")

    def _on_weather_command(self, line: str):
        """仅在串口在线时记录并发送天气相关命令。"""
        if self.serial_worker._connected:
            self._on_send_command(line)

    def _weather_fetch_callback(self):
        """定时器回调：自动刷新天气"""
        self._request_weather_fetch("timer")

    def _initial_weather_fetch(self):
        """启动后首次拉取天气"""
        self._request_weather_fetch("initial")

    def _on_dashboard(self):
        """切换数据看板的打开/关闭状态。"""
        self._show_dashboard_dialog()

    def _show_dashboard_dialog(self):
        """以单实例方式打开数据看板；已打开时再次调用则关闭。"""
        if self._dashboard_window is not None:
            self._dashboard_window.close()
            return

        from dashboard import DashboardWindow

        # 确保至少有一条数据展示效果 (否则空图表让人困惑)
        events = self.dashboard.load_events()
        if not events:
            self.status_bar.showMessage("暂无事件数据 — 开始记录中...", 3000)

        win = DashboardWindow(self.dashboard, self)
        win.setWindowTitle("数据可视化看板 (E4)")
        win.setAttribute(Qt.WA_DeleteOnClose)
        win.destroyed.connect(self._on_dashboard_closed)
        self._dashboard_window = win
        win.show()
        self.btn_dashboard.setText("⌁  关闭看板")
        self.btn_dashboard.setToolTip("关闭数据看板")
        self.status_bar.showMessage("数据看板 — 事件记录保存在 logs/events.csv", 3000)

    def _on_dashboard_closed(self):
        """看板销毁后恢复按钮和单实例状态。"""
        self._dashboard_window = None
        self.btn_dashboard.setText("⌁  数据看板")
        self.btn_dashboard.setToolTip("查看事件统计图表")

    def _on_auto_daynight_toggled(self, checked: bool):
        """启停基于上海日出日落的自动模式。"""
        if not self.auto_daynight.set_enabled(checked) and checked:
            self.chk_auto_daynight.blockSignals(True)
            self.chk_auto_daynight.setChecked(False)
            self.chk_auto_daynight.blockSignals(False)
            self.chk_auto_daynight.setEnabled(False)
            return

        state = "已开启" if checked else "已关闭"
        self.status_bar.showMessage(f"自动昼夜模式{state}", 3000)

    def _on_auto_daynight_enabled_changed(self, enabled: bool):
        """同步模块状态与工具栏开关。"""
        if self.chk_auto_daynight.isChecked() != enabled:
            self.chk_auto_daynight.blockSignals(True)
            self.chk_auto_daynight.setChecked(enabled)
            self.chk_auto_daynight.blockSignals(False)

    def _on_daynight_schedule_updated(
        self, target_mode: str, sunrise: str, sunset: str
    ):
        """显示当天日出、日落及当前自动目标。"""
        self.sun_schedule_label.setText(f"☀ {sunrise}  ☾ {sunset}")
        self.sun_schedule_label.setToolTip(
            f"上海日出 {sunrise} / 日落 {sunset}\n当前自动目标：{target_mode}"
        )
        self.sun_schedule_label.setProperty("mode", target_mode.lower())
        self.sun_schedule_label.style().unpolish(self.sun_schedule_label)
        self.sun_schedule_label.style().polish(self.sun_schedule_label)

    def _on_auto_daynight_command(self, command: str, target_mode: str):
        """仅在串口在线时下发自动 DAY/NIGHT 命令。"""
        if not self.serial_worker._connected:
            return
        self._on_send_command(command)
        self.status_bar.showMessage(
            f"自动昼夜：已切换到 {target_mode}", 4000
        )
        self.dashboard.log_event("MODE", "AUTO", target_mode)

    def _on_auto_daynight_error(self, message: str):
        """自动昼夜依赖缺失或计算失败。"""
        if not self.auto_daynight.available:
            self.chk_auto_daynight.setEnabled(False)
            self.sun_schedule_label.setText("自动昼夜不可用")
        self.log_panel.add_error(message)
        self.status_bar.showMessage(message, 8000)

    # ═══════════════════════════════════════════════════════════════
    # 命令发送处理
    # ═══════════════════════════════════════════════════════════════

    def _on_send_command(self, line: str):
        """命令发送: 记录 TX 日志 → 转发到串口线程"""
        self.log_panel.add_tx_command(line)
        self._track_expected_response(line)
        self.serial_worker.send_line(line)

    def _track_expected_response(self, line: str):
        """记录应答顺序，用于解释通用的 OK/ERROR 响应。"""
        upper = line.strip().upper()
        if not upper or upper.startswith("*PING") or upper.startswith("INIT"):
            return
        self._pending_responses.append(upper)

    def _send_status_query(self, line: str):
        """发送不刷 TX 日志的后台状态查询。"""
        if not self.serial_worker._connected:
            return
        upper = line.upper()
        if upper in self._pending_responses:
            return
        self._track_expected_response(line)
        self.serial_worker.send_line(line)

    def _poll_device_status(self):
        """周期查询 FORMAT 和 ALARM；MODE 由 MCU 事件实时更新。"""
        if not self.serial_worker._connected:
            return
        self._send_status_query(self.protocol.build_get_format())
        QTimer.singleShot(
            120,
            lambda: self._send_status_query(self.protocol.build_get_alarm()),
        )

    # ═══════════════════════════════════════════════════════════════
    # 接收行处理
    # ═══════════════════════════════════════════════════════════════

    def _on_line_received(self, line: str):
        """收到一行完整的 MCU 响应/事件"""
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
                # E1: 与工具栏 NTP 按钮完全相同的对时流程
                self._request_ntp_sync("USER1")

            elif key_name == "USER2":
                # E2: USER2 触发天气短显
                if not self.weather_helper.send_weather_to_mcu():
                    self._request_weather_fetch("USER2", send_to_mcu=True)

        elif etype == "MODE":
            state = event.get("state", "")
            self.twin_panel.update_mode(state)
            self._update_status_bar_mode(state)
            self.auto_daynight.note_device_mode(state)

        elif etype == "ALARM":
            self._set_alarm_status(True, ringing=True)
            self.dashboard.log_event("ALARM")

        elif etype == "ALARM_OFF":
            # 停止响铃不等于关闭闹钟。
            self._set_alarm_status(True)

        elif etype == "EDIT":
            # 记录到 dashboard CSV
            field = event.get("field", "")
            value = event.get("value", "")
            self.dashboard.log_event("EDIT", field, value)
            if field.upper() == "ALARM":
                self._update_alarm_from_value(value)

        elif etype == "UNKNOWN":
            pass  # 未知事件类型，仅记录日志

    def _parse_response(self, line: str):
        """解析非事件响应行，更新状态栏和缓存"""
        upper = line.upper().strip()

        pending = ""
        if upper == "OK" or upper.startswith("OK ") or upper.startswith("ERROR"):
            if self._pending_responses:
                pending = self._pending_responses.popleft()

        if upper.startswith("ERROR"):
            return

        payload = line.strip()[2:].strip() if upper.startswith("OK") else ""

        if pending.startswith("*GET:FORMAT"):
            self._update_format_status(payload)
            return

        if pending.startswith("*GET:ALARM"):
            self._update_alarm_from_value(payload)
            return

        if pending.startswith("*SET:FORMAT"):
            if " RIGHT" in pending:
                self._update_format_status("RIGHT")
            elif " LEFT" in pending:
                self._update_format_status("LEFT")
            return

        if pending.startswith("*SET:ALARM"):
            self._set_alarm_status(True)
            return

        if pending.startswith("*SET:MODE"):
            target = pending.rsplit(" ", 1)[-1]
            self._update_status_bar_mode(target)
            return

        if pending.startswith("*RST"):
            self._update_format_status("LEFT")
            self._update_status_bar_mode("FLOWING")
            return

        if upper.startswith("FORMAT:"):
            fmt = line.split(":", 1)[1].strip().upper()
            self._update_format_status(fmt)

        elif upper.startswith("ALARM:"):
            self._update_alarm_from_value(line.split(":", 1)[1].strip())

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

    def _update_format_status(self, value: str):
        """更新 FORMAT 实际状态。"""
        fmt = value.strip().upper()
        if fmt not in ("LEFT", "RIGHT"):
            return
        self._current_format = fmt
        self.fmt_label.setText(f"FORMAT: {fmt}")
        self._set_widget_state(self.fmt_label, "active")

    def _update_alarm_from_value(self, value: str):
        """根据 GET/EDIT 返回值更新闹钟使能状态。"""
        normalized = value.strip().upper()
        disabled_values = {
            "", "XX.XX.XX", "XX:XX:XX", "--.--.--", "--:--:--",
            "DISABLED", "OFF",
        }
        if normalized in disabled_values or "X" in normalized:
            self._alarm_time = ""
            self._set_alarm_status(False)
            return

        self._alarm_time = value.strip().replace(".", ":")
        self._set_alarm_status(True)

    def _set_alarm_status(self, enabled: bool | None, ringing: bool = False):
        """状态栏只展示 ALARM 是否使能，时间放在提示中。"""
        self._alarm_enabled = enabled
        if enabled is None:
            self.alarm_label.setText("ALARM: --")
            self.alarm_label.setToolTip("尚未读取闹钟状态")
            self._set_widget_state(self.alarm_label, "unknown")
        elif enabled:
            suffix = " · RINGING" if ringing else ""
            self.alarm_label.setText(f"ALARM: ENABLED{suffix}")
            tooltip = "闹钟已使能"
            if self._alarm_time:
                tooltip += f"\n设定时间：{self._alarm_time}"
            self.alarm_label.setToolTip(tooltip)
            self._set_widget_state(
                self.alarm_label, "danger" if ringing else "active"
            )
        else:
            self.alarm_label.setText("ALARM: DISABLED")
            self.alarm_label.setToolTip("闹钟未设置")
            self._set_widget_state(self.alarm_label, "disabled")

    def _update_status_bar_mode(self, state: str):
        """根据 MCU 上报的状态更新 MODE 标签"""
        upper = state.upper()
        mode_map = {
            "FLOWING": "FLOWING",
            "DATE": "DATE",
            "TIME": "TIME",
            "ALARM": "ALARM",
            "NIGHT": "NIGHT",
            "DAY": "DAY",
        }
        display_mode = mode_map.get(upper, upper)
        if not display_mode:
            return
        self._current_mode = display_mode
        self.mode_label.setText(f"MODE: {display_mode}")
        state_name = "night" if display_mode == "NIGHT" else "active"
        self._set_widget_state(self.mode_label, state_name)

    def _on_connection_changed(self, connected: bool):
        """串口连接状态变更"""
        port = self.serial_worker.port_name
        if connected:
            self.conn_label.setText(f"●  ONLINE  ·  {port}")
            self._set_widget_state(self.conn_label, "online")
            self.btn_connect.setText("断开")
            self.btn_connect.setProperty("danger", True)
            self.weather_helper.send_status_led_to_mcu()
            self._pending_responses.clear()
            QTimer.singleShot(250, self._poll_device_status)
            QTimer.singleShot(600, self.auto_daynight.sync_now)
        else:
            self.conn_label.setText("●  OFFLINE")
            self._set_widget_state(self.conn_label, "offline")
            self.btn_connect.setText("连接")
            self.btn_connect.setProperty("danger", False)
            self._pending_responses.clear()
            self._current_format = ""
            self._current_mode = ""
            self.fmt_label.setText("FORMAT: --")
            self.mode_label.setText("MODE: --")
            self._set_widget_state(self.fmt_label, "unknown")
            self._set_widget_state(self.mode_label, "unknown")
            self._set_alarm_status(None)
            # 如果有端口错误，显示在状态栏和日志
            err = self.serial_worker.last_port_error
            if err:
                self.status_bar.showMessage("端口错误: " + err, 8000)
                self.log_panel.add_error("端口错误: " + err)

        self.btn_connect.style().unpolish(self.btn_connect)
        self.btn_connect.style().polish(self.btn_connect)

        # 启用/禁用控制面板控件
        self.control_panel.set_controls_enabled(connected)

    def _on_port_error(self, error_msg: str):
        """端口错误信号处理"""
        if not error_msg or not error_msg.strip():
            return
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

        # 停止状态轮询
        self._status_poll_timer.stop()

        # 停止自动昼夜模式定时器
        self.auto_daynight.stop()

        event.accept()


def main():
    app = QApplication(sys.argv)
    app.setStyle("Fusion")

    theme_path = os.path.join(os.path.dirname(__file__), "theme.qss")
    if os.path.exists(theme_path):
        with open(theme_path, "r", encoding="utf-8") as f:
            app.setStyleSheet(f.read())

    window = MainWindow()
    window.show()

    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
