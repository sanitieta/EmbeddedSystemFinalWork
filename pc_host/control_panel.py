"""
控制面板
========
按任务组织全部串口命令：
- 时间与闹钟：日期、时间、闹钟
- 显示与输出：显示策略、消息、LED、蜂鸣
- 设备与诊断：电机、系统命令、协议演示
"""

from PyQt5.QtCore import Qt, pyqtSignal
from PyQt5.QtWidgets import (
    QComboBox,
    QFrame,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QScrollArea,
    QSpinBox,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)

from protocol import Protocol


class ControlPanel(QWidget):
    """按工作流分组的设备控制面板。"""

    send_command = pyqtSignal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("ControlPanel")
        self.protocol = Protocol()

        self._build_ui()
        self._connect_signals()
        self._apply_styles()

    # ── 顶层结构 ──

    def _build_ui(self):
        outer = QVBoxLayout(self)
        outer.setContentsMargins(18, 16, 18, 14)
        outer.setSpacing(10)

        header = QHBoxLayout()
        heading = QVBoxLayout()
        heading.setSpacing(1)
        eyebrow = QLabel("COMMAND WORKSPACE")
        eyebrow.setObjectName("panelEyebrow")
        title = QLabel("控制中心")
        title.setObjectName("panelTitle")
        heading.addWidget(eyebrow)
        heading.addWidget(title)
        header.addLayout(heading)
        header.addStretch()
        badge = QLabel("3 WORKFLOWS")
        badge.setObjectName("sectionBadge")
        header.addWidget(badge, alignment=Qt.AlignVCenter)
        outer.addLayout(header)

        self.control_tabs = QTabWidget()
        self.control_tabs.setObjectName("controlTabs")
        self.control_tabs.setDocumentMode(True)
        self.control_tabs.addTab(
            self._build_datetime_tab(), "时间与闹钟"
        )
        self.control_tabs.addTab(
            self._build_output_tab(), "显示与输出"
        )
        self.control_tabs.addTab(
            self._build_device_tab(), "设备与诊断"
        )
        outer.addWidget(self.control_tabs)

    def _new_tab(self, hint: str):
        """创建带独立滚动区域的标签页，返回内容布局。"""
        scroll = QScrollArea()
        scroll.setObjectName("controlTabScroll")
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)

        content = QWidget()
        content.setObjectName("controlTabContent")
        layout = QVBoxLayout(content)
        layout.setContentsMargins(4, 8, 8, 8)
        layout.setSpacing(10)

        hint_label = QLabel(hint)
        hint_label.setObjectName("controlTabHint")
        hint_label.setWordWrap(True)
        layout.addWidget(hint_label)
        scroll.setWidget(content)
        return scroll, layout

    @staticmethod
    def _group(title: str, layout) -> QGroupBox:
        group = QGroupBox(title)
        group.setProperty("controlCard", True)
        group.setLayout(layout)
        return group

    @staticmethod
    def _configure_columns(layout: QGridLayout, count: int = 6):
        for column in range(count):
            layout.setColumnStretch(column, 1)

    # ── 时间与闹钟 ──

    def _build_datetime_tab(self):
        page, layout = self._new_tab(
            "集中管理 RTC 日期、时间与闹钟。字段组合用于验证 MCU 的部分字段命令。"
        )
        layout.addWidget(self._build_date_group())
        layout.addWidget(self._build_time_group())
        layout.addWidget(self._build_alarm_group())
        layout.addStretch()
        return page

    def _build_date_group(self):
        grid = QGridLayout()
        self._configure_columns(grid)
        grid.setHorizontalSpacing(8)
        grid.setVerticalSpacing(8)

        self.date_year = QSpinBox()
        self.date_year.setRange(2000, 2099)
        self.date_year.setValue(2025)
        self.date_month = QSpinBox()
        self.date_month.setRange(1, 12)
        self.date_day = QSpinBox()
        self.date_day.setRange(1, 31)

        for column, (label, widget) in enumerate((
            ("年", self.date_year),
            ("月", self.date_month),
            ("日", self.date_day),
        )):
            start = column * 2
            grid.addWidget(QLabel(label), 0, start)
            grid.addWidget(widget, 0, start + 1)

        self.date_combo = QComboBox()
        self.date_combo.addItems([
            "完整日期 · YEAR MONTH DATE",
            "年 / 月 · YEAR MONTH",
            "年 / 日 · YEAR DATE",
            "月 / 日 · MONTH DATE",
            "仅年份 · YEAR",
            "仅月份 · MONTH",
            "仅日期 · DATE",
        ])
        self.btn_date_set = QPushButton("应用日期")
        self.btn_date_get = QPushButton("读取日期")
        grid.addWidget(QLabel("字段"), 1, 0)
        grid.addWidget(self.date_combo, 1, 1, 1, 3)
        grid.addWidget(self.btn_date_set, 1, 4)
        grid.addWidget(self.btn_date_get, 1, 5)
        return self._group("日期  /  SET:DATE", grid)

    def _build_time_group(self):
        grid = QGridLayout()
        self._configure_columns(grid)
        grid.setHorizontalSpacing(8)
        grid.setVerticalSpacing(8)

        self.time_hour = QSpinBox()
        self.time_hour.setRange(0, 23)
        self.time_minute = QSpinBox()
        self.time_minute.setRange(0, 59)
        self.time_second = QSpinBox()
        self.time_second.setRange(0, 59)

        for column, (label, widget) in enumerate((
            ("时", self.time_hour),
            ("分", self.time_minute),
            ("秒", self.time_second),
        )):
            start = column * 2
            grid.addWidget(QLabel(label), 0, start)
            grid.addWidget(widget, 0, start + 1)

        self.time_combo = QComboBox()
        self.time_combo.addItems([
            "完整时间 · HOUR MINUTE SECOND",
            "时 / 分 · HOUR MINUTE",
            "时 / 秒 · HOUR SECOND",
            "分 / 秒 · MINUTE SECOND",
            "仅小时 · HOUR",
            "仅分钟 · MINUTE",
            "仅秒钟 · SECOND",
        ])
        self.btn_time_set = QPushButton("应用时间")
        self.btn_time_get = QPushButton("读取时间")
        grid.addWidget(QLabel("字段"), 1, 0)
        grid.addWidget(self.time_combo, 1, 1, 1, 3)
        grid.addWidget(self.btn_time_set, 1, 4)
        grid.addWidget(self.btn_time_get, 1, 5)
        return self._group("时间  /  SET:TIME", grid)

    def _build_alarm_group(self):
        grid = QGridLayout()
        self._configure_columns(grid)
        grid.setHorizontalSpacing(8)
        grid.setVerticalSpacing(8)

        self.alarm_hour = QSpinBox()
        self.alarm_hour.setRange(0, 23)
        self.alarm_minute = QSpinBox()
        self.alarm_minute.setRange(0, 59)
        self.alarm_second = QSpinBox()
        self.alarm_second.setRange(0, 59)

        for column, (label, widget) in enumerate((
            ("时", self.alarm_hour),
            ("分", self.alarm_minute),
            ("秒", self.alarm_second),
        )):
            start = column * 2
            grid.addWidget(QLabel(label), 0, start)
            grid.addWidget(widget, 0, start + 1)

        self.btn_alarm_set = QPushButton("启用闹钟")
        self.btn_alarm_off = QPushButton("关闭闹钟")
        self.btn_alarm_get = QPushButton("读取状态")
        grid.addWidget(self.btn_alarm_set, 1, 0, 1, 2)
        grid.addWidget(self.btn_alarm_off, 1, 2, 1, 2)
        grid.addWidget(self.btn_alarm_get, 1, 4, 1, 2)
        return self._group("闹钟  /  SET:ALARM", grid)

    # ── 显示与输出 ──

    def _build_output_tab(self):
        page, layout = self._new_tab(
            "控制数码管显示策略和板端输出。LED 接管为独占模式，00 恢复固件默认状态。"
        )
        layout.addWidget(self._build_display_group())
        layout.addWidget(self._build_message_group())
        layout.addWidget(self._build_led_group())
        layout.addWidget(self._build_beep_group())
        layout.addStretch()
        return page

    def _build_display_group(self):
        grid = QGridLayout()
        self._configure_columns(grid, 5)
        grid.setHorizontalSpacing(8)
        grid.setVerticalSpacing(8)

        self.btn_disp_on = QPushButton("开启")
        self.btn_disp_off = QPushButton("关闭")
        self.btn_get_display = QPushButton("读取")
        self.btn_format_left = QPushButton("LEFT")
        self.btn_format_right = QPushButton("RIGHT")
        self.btn_get_format = QPushButton("读取")
        self.btn_mode_day = QPushButton("DAY")
        self.btn_mode_night = QPushButton("NIGHT")

        grid.addWidget(QLabel("显示电源"), 0, 0)
        grid.addWidget(self.btn_disp_on, 0, 1)
        grid.addWidget(self.btn_disp_off, 0, 2)
        grid.addWidget(self.btn_get_display, 0, 4)
        grid.addWidget(QLabel("显示方向"), 1, 0)
        grid.addWidget(self.btn_format_left, 1, 1)
        grid.addWidget(self.btn_format_right, 1, 2)
        grid.addWidget(self.btn_get_format, 1, 4)
        grid.addWidget(QLabel("昼夜模式"), 2, 0)
        grid.addWidget(self.btn_mode_day, 2, 1)
        grid.addWidget(self.btn_mode_night, 2, 2)
        return self._group("显示策略  /  DISPLAY · FORMAT · MODE", grid)

    def _build_message_group(self):
        row = QHBoxLayout()
        self.msg_input = QLineEdit()
        self.msg_input.setMaxLength(32)
        self.msg_input.setPlaceholderText("输入最多 32 个 ASCII 字符")
        self.btn_msg_send = QPushButton("发送到数码管")
        row.addWidget(QLabel("消息"))
        row.addWidget(self.msg_input, 1)
        row.addWidget(self.btn_msg_send)
        return self._group("临时消息  /  SET:MSG", row)

    def _build_led_group(self):
        row = QHBoxLayout()
        self.led_input = QLineEdit()
        self.led_input.setMaxLength(2)
        self.led_input.setFixedWidth(72)
        self.led_input.setPlaceholderText("FF")
        self.led_input.setToolTip("输入 00–FF；00 退出接管")
        self.btn_led_set = QPushButton("应用接管")
        self.btn_led_default = QPushButton("恢复默认")
        row.addWidget(QLabel("HEX"))
        row.addWidget(self.led_input)
        row.addWidget(self.btn_led_set)
        row.addWidget(self.btn_led_default)
        row.addStretch()
        return self._group("LED 接管  /  SET:LED", row)

    def _build_beep_group(self):
        row = QHBoxLayout()
        self.spin_beep_ms = QSpinBox()
        self.spin_beep_ms.setRange(10, 5000)
        self.spin_beep_ms.setValue(200)
        self.spin_beep_ms.setSuffix(" ms")
        self.btn_beep = QPushButton("触发蜂鸣")
        row.addWidget(QLabel("持续时间"))
        row.addWidget(self.spin_beep_ms)
        row.addWidget(self.btn_beep)
        row.addStretch()
        return self._group("远程蜂鸣  /  SET:BEEP", row)

    # ── 设备与诊断 ──

    def _build_device_tab(self):
        page, layout = self._new_tab(
            "设备动作与协议诊断。INIT 会重启 MCU，执行前请确认当前设置已保存。"
        )
        layout.addWidget(self._build_motor_group())
        layout.addWidget(self._build_system_group())
        layout.addWidget(self._build_demo_group())
        layout.addStretch()
        return page

    def _build_motor_group(self):
        grid = QGridLayout()
        self._configure_columns(grid, 5)
        self.btn_motor_start = QPushButton("START")
        self.btn_motor_stop = QPushButton("STOP")
        self.btn_motor_fwd = QPushButton("FWD")
        self.btn_motor_rev = QPushButton("REV")
        self.btn_motor_get = QPushButton("读取状态")
        for column, button in enumerate((
            self.btn_motor_start,
            self.btn_motor_stop,
            self.btn_motor_fwd,
            self.btn_motor_rev,
            self.btn_motor_get,
        )):
            grid.addWidget(button, 0, column)
        return self._group("步进电机  /  MOTION", grid)

    def _build_system_group(self):
        grid = QGridLayout()
        self._configure_columns(grid, 4)
        self.btn_rst = QPushButton("复位协议状态")
        self.btn_init = QPushButton("重启 MCU")
        self.btn_help = QPushButton("命令帮助")
        self.btn_ping = QPushButton("链路测试")
        for column, button in enumerate((
            self.btn_rst, self.btn_init, self.btn_help, self.btn_ping
        )):
            grid.addWidget(button, 0, column)
        self.btn_rst.setToolTip("*RST：恢复协议显示状态，不重启 MCU")
        self.btn_init.setToolTip("INIT：执行 MCU 软件复位")
        self.btn_help.setToolTip("HELP：输出完整命令列表")
        self.btn_ping.setToolTip("*PING：请求 MCU 返回 *PONG")
        return self._group("系统诊断  /  SYSTEM", grid)

    def _build_demo_group(self):
        layout = QVBoxLayout()
        hint = QLabel("用于演示协议的最小匹配与大小写不敏感特性。")
        hint.setObjectName("controlCardHint")
        buttons = QHBoxLayout()
        self.btn_demo_abbrev = QPushButton("最小匹配演示")
        self.btn_demo_case = QPushButton("大小写混合演示")
        buttons.addWidget(self.btn_demo_abbrev)
        buttons.addWidget(self.btn_demo_case)
        layout.addWidget(hint)
        layout.addLayout(buttons)
        return self._group("协议演示  /  PARSER", layout)

    # ── 信号连接 ──

    def _connect_signals(self):
        self.btn_date_set.clicked.connect(self._on_date_set)
        self.btn_date_get.clicked.connect(self._on_date_get)
        self.btn_time_set.clicked.connect(self._on_time_set)
        self.btn_time_get.clicked.connect(self._on_time_get)
        self.btn_alarm_set.clicked.connect(self._on_alarm_set)
        self.btn_alarm_off.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_set_alarm_off()
        ))
        self.btn_alarm_get.clicked.connect(self._on_alarm_get)

        self.btn_disp_on.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_set_display(True)
        ))
        self.btn_disp_off.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_set_display(False)
        ))
        self.btn_format_left.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_set_format(True)
        ))
        self.btn_format_right.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_set_format(False)
        ))
        self.btn_mode_day.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_set_mode(False)
        ))
        self.btn_mode_night.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_set_mode(True)
        ))
        self.btn_get_display.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_get_display()
        ))
        self.btn_get_format.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_get_format()
        ))

        self.btn_msg_send.clicked.connect(self._on_msg_send)
        self.msg_input.returnPressed.connect(self._on_msg_send)
        self.btn_led_set.clicked.connect(self._on_led_set)
        self.led_input.returnPressed.connect(self._on_led_set)
        self.btn_led_default.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_set_led(0x00)
        ))
        self.btn_beep.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_set_beep(self.spin_beep_ms.value())
        ))

        self.btn_motor_start.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_motor_start()
        ))
        self.btn_motor_stop.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_motor_stop()
        ))
        self.btn_motor_fwd.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_motor_fwd()
        ))
        self.btn_motor_rev.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_motor_rev()
        ))
        self.btn_motor_get.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_get_motor()
        ))

        self.btn_rst.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_rst()
        ))
        self.btn_init.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_init()
        ))
        self.btn_help.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_help()
        ))
        self.btn_ping.clicked.connect(lambda: self.send_command.emit("*PING"))
        self.btn_demo_abbrev.clicked.connect(self._on_demo_abbrev)
        self.btn_demo_case.clicked.connect(self._on_demo_case)

    # ── 命令构建 ──

    def _on_date_set(self):
        index = self.date_combo.currentIndex()
        year = self.date_year.value()
        month = self.date_month.value()
        day = self.date_day.value()
        builders = (
            lambda: self.protocol.build_set_date(year=year, month=month, day=day),
            lambda: self.protocol.build_set_date(year=year, month=month),
            lambda: self.protocol.build_set_date(year=year, day=day),
            lambda: self.protocol.build_set_date(month=month, day=day),
            lambda: self.protocol.build_set_date(year=year),
            lambda: self.protocol.build_set_date(month=month),
            lambda: self.protocol.build_set_date(day=day),
        )
        self.send_command.emit(builders[index]())

    def _on_date_get(self):
        self.send_command.emit(self.protocol.build_get_date())

    def _on_time_set(self):
        index = self.time_combo.currentIndex()
        hour = self.time_hour.value()
        minute = self.time_minute.value()
        second = self.time_second.value()
        builders = (
            lambda: self.protocol.build_set_time(
                hour=hour, minute=minute, second=second
            ),
            lambda: self.protocol.build_set_time(hour=hour, minute=minute),
            lambda: self.protocol.build_set_time(hour=hour, second=second),
            lambda: self.protocol.build_set_time(minute=minute, second=second),
            lambda: self.protocol.build_set_time(hour=hour),
            lambda: self.protocol.build_set_time(minute=minute),
            lambda: self.protocol.build_set_time(second=second),
        )
        self.send_command.emit(builders[index]())

    def _on_time_get(self):
        self.send_command.emit(self.protocol.build_get_time())

    def _on_alarm_set(self):
        self.send_command.emit(self.protocol.build_set_alarm(
            hour=self.alarm_hour.value(),
            minute=self.alarm_minute.value(),
            second=self.alarm_second.value(),
        ))

    def _on_alarm_get(self):
        self.send_command.emit(self.protocol.build_get_alarm())

    def _on_msg_send(self):
        text = self.msg_input.text().strip()
        if text:
            self.send_command.emit(self.protocol.build_set_msg(text))

    def _on_led_set(self):
        text = self.led_input.text().strip()
        if not text:
            return
        try:
            self.send_command.emit(self.protocol.build_set_led(int(text, 16)))
        except ValueError:
            self.led_input.setFocus()

    def _on_demo_abbrev(self):
        """发送最小匹配缩写命令。"""
        self.send_command.emit("*SET:TIME MIN SEC 11 22")

    def _on_demo_case(self):
        """发送大小写混合命令。"""
        self.send_command.emit("*SeT:DaTe YeAr MoNtH dAtE 2025 6 15")

    # ── 语义样式与使能 ──

    def _apply_styles(self):
        for button in (
            self.btn_date_set,
            self.btn_time_set,
            self.btn_alarm_set,
            self.btn_msg_send,
            self.btn_led_set,
            self.btn_motor_start,
            self.btn_beep,
        ):
            button.setProperty("primary", True)

        for button in (
            self.btn_date_get,
            self.btn_time_get,
            self.btn_alarm_get,
            self.btn_get_display,
            self.btn_get_format,
            self.btn_motor_get,
            self.btn_led_default,
        ):
            button.setProperty("quiet", True)

        self.btn_alarm_off.setProperty("danger", True)
        self.btn_motor_stop.setProperty("danger", True)
        self.btn_init.setProperty("danger", True)
        self.btn_rst.setProperty("warning", True)
        self.btn_disp_off.setProperty("warning", True)

        for button in (self.btn_demo_abbrev, self.btn_demo_case):
            button.setProperty("demo", True)

    def set_controls_enabled(self, enabled: bool):
        """串口连接/断开时统一启用或禁用所有命令按钮。"""
        for button in self.findChildren(QPushButton):
            button.setEnabled(enabled)
