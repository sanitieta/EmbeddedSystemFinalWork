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

from PyQt5.QtWidgets import QApplication, QMainWindow, QMessageBox
from PyQt5.QtCore import Qt

# ---- UI generated from Qt Designer (placeholder until .ui is designed) ----
# from ui_main_window import Ui_MainWindow

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

        # ---- 核心组件 ----
        self.serial_worker = SerialWorker()
        self.protocol = Protocol()

        # ---- UI 面板 (先占位，逐步实现) ----
        # TODO: 加载 .ui 文件并 setupUi
        # self.ui = Ui_MainWindow()
        # self.ui.setupUi(self)

        self.twin_panel = TwinPanel(self)
        self.control_panel = ControlPanel(self)
        self.log_panel = LogPanel(self)

        # ---- 扩展功能模块 ----
        self.ntp_helper = NtpHelper(self.serial_worker)
        self.weather_helper = WeatherHelper(self.serial_worker)
        self.auto_daynight = AutoDayNight(self.serial_worker)
        self.dashboard = Dashboard()

        # ---- 连接信号 ----
        self._connect_signals()

        # ---- 启动后台线程 ----
        self.serial_worker.start()

    def _connect_signals(self):
        """连接 SerialWorker 信号到各面板"""
        sw = self.serial_worker

        # 收到完整行 → 协议解析 → 分发到各面板
        sw.line_received.connect(self._on_line_received)

        # 连接状态
        sw.connection_changed.connect(self._on_connection_changed)
        sw.latency_updated.connect(self._on_latency_updated)

        # 控制面板的发送请求 → SerialWorker
        self.control_panel.send_command.connect(self.serial_worker.send_line)

        # 虚拟按键点击 → SerialWorker (*SET:KEY)
        self.twin_panel.virtual_key_pressed.connect(self.serial_worker.send_line)

    def _on_line_received(self, line: str):
        """收到一行完整的 MCU 响应/事件"""
        # 记录日志
        event = self.protocol.parse_event(line)
        if event:
            self.log_panel.add_rx_event(line)
            self._dispatch_event(event)
        else:
            self.log_panel.add_rx_response(line)

    def _dispatch_event(self, event: dict):
        """将解析后的事件分发到各面板"""
        etype = event.get("type")

        if etype == "DISP":
            self.twin_panel.update_display(event["chars"], event["dp"])
        elif etype == "LED":
            self.twin_panel.update_leds(event["pattern"])
        elif etype == "KEY":
            self.twin_panel.highlight_key(event["name"])
            # E1: USER1 自动触发 NTP 对时
            if event["name"] == "USER1":
                self.ntp_helper.sync_from_user1()
            # E2: USER2 触发天气短显
            elif event["name"] == "USER2":
                self.weather_helper.send_weather_to_mcu()
        elif etype == "MODE":
            self.twin_panel.update_mode(event["state"])
            self._update_status_bar_mode(event["state"])
        elif etype == "ALARM":
            self._update_status_bar_alarm("RINGING")
        elif etype == "ALARM_OFF":
            self._update_status_bar_alarm("OFF")
        elif etype == "EDIT":
            pass  # logged already

    def _on_connection_changed(self, connected: bool):
        port = self.serial_worker.port_name
        status = f"● 已连接 {port}" if connected else "○ 未连接"
        # TODO: update status bar
        self.control_panel.set_controls_enabled(connected)

    def _on_latency_updated(self, latency_ms: int):
        # TODO: update status bar latency display
        pass

    def _update_status_bar_mode(self, state: str):
        pass  # TODO

    def _update_status_bar_alarm(self, state: str):
        pass  # TODO

    def closeEvent(self, event):
        """窗口关闭时停止后台线程"""
        self.serial_worker.stop()
        self.serial_worker.wait(2000)
        event.accept()


def main():
    app = QApplication(sys.argv)
    app.setStyle("Fusion")

    window = MainWindow()
    window.show()

    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
