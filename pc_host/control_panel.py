"""
控制面板
========
覆盖全部串口命令的可视化下发:
- 时间/日期/闹钟 设置与获取
- 显示 ON/OFF、FORMAT LEFT/RIGHT
- 消息发送、LED 接管
- 电机控制
- 系统命令 (RST/INIT)

演示功能:
- 参数组合下拉 (≥3 种)
- 缩写演示按钮
- 大小写混合演示按钮
"""

from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGridLayout, QGroupBox,
    QPushButton, QLineEdit, QComboBox, QLabel, QSpinBox,
    QFrame, QScrollArea,
)
from PyQt5.QtCore import Qt, pyqtSignal
from protocol import Protocol


class ControlPanel(QWidget):
    """命令控制面板"""

    send_command = pyqtSignal(str)  # 连接到 SerialWorker.send_line

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("ControlPanel")
        self.setWindowTitle("控制面板")
        self.protocol = Protocol()

        self._build_ui()
        self._connect_signals()
        self._apply_styles()

    # ── UI 构建 ──

    def _build_ui(self):
        outer_layout = QVBoxLayout(self)
        outer_layout.setContentsMargins(18, 16, 18, 14)
        outer_layout.setSpacing(10)

        header = QHBoxLayout()
        header_text = QVBoxLayout()
        header_text.setSpacing(1)
        title = QLabel("控制中心")
        title.setObjectName("panelTitle")
        subtitle = QLabel("COMMAND & AUTOMATION")
        subtitle.setObjectName("panelEyebrow")
        header_text.addWidget(subtitle)
        header_text.addWidget(title)
        header.addLayout(header_text)
        header.addStretch()
        command_badge = QLabel("22+ COMMANDS")
        command_badge.setObjectName("sectionBadge")
        header.addWidget(command_badge, alignment=Qt.AlignVCenter)
        outer_layout.addLayout(header)

        scroll = QScrollArea()
        scroll.setObjectName("controlScroll")
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        content = QWidget()
        content.setObjectName("controlContent")
        layout = QVBoxLayout(content)
        layout.setContentsMargins(0, 0, 6, 0)
        layout.setSpacing(10)

        # === 日期设置组 ===
        date_group = QGroupBox("日期  /  SET:DATE")
        date_layout = QGridLayout()
        date_layout.addWidget(QLabel("年份:"), 0, 0)
        self.date_year = QSpinBox()
        self.date_year.setRange(2000, 2099)
        self.date_year.setValue(2025)
        date_layout.addWidget(self.date_year, 0, 1)

        date_layout.addWidget(QLabel("月份:"), 0, 2)
        self.date_month = QSpinBox()
        self.date_month.setRange(1, 12)
        date_layout.addWidget(self.date_month, 0, 3)

        date_layout.addWidget(QLabel("日期:"), 0, 4)
        self.date_day = QSpinBox()
        self.date_day.setRange(1, 31)
        date_layout.addWidget(self.date_day, 0, 5)

        date_layout.addWidget(QLabel("参数组合:"), 1, 0)
        self.date_combo = QComboBox()
        self.date_combo.addItems([
            "YEAR MONTH DATE (完整)",
            "YEAR MONTH (年月)",
            "YEAR DATE (年日)",
            "MONTH DATE (月日)",
            "YEAR (仅年)",
            "MONTH (仅月)",
            "DATE (仅日)",
        ])
        date_layout.addWidget(self.date_combo, 1, 1, 1, 3)

        self.btn_date_set = QPushButton("设置日期")
        self.btn_date_get = QPushButton("获取日期")
        date_layout.addWidget(self.btn_date_set, 1, 4)
        date_layout.addWidget(self.btn_date_get, 1, 5)
        date_group.setLayout(date_layout)
        layout.addWidget(date_group)
        layout.addWidget(self._make_separator())

        # === 时间设置组 ===
        time_group = QGroupBox("时间  /  SET:TIME")
        time_layout = QGridLayout()
        time_layout.addWidget(QLabel("时:"), 0, 0)
        self.time_hour = QSpinBox()
        self.time_hour.setRange(0, 23)
        time_layout.addWidget(self.time_hour, 0, 1)

        time_layout.addWidget(QLabel("分:"), 0, 2)
        self.time_minute = QSpinBox()
        self.time_minute.setRange(0, 59)
        time_layout.addWidget(self.time_minute, 0, 3)

        time_layout.addWidget(QLabel("秒:"), 0, 4)
        self.time_second = QSpinBox()
        self.time_second.setRange(0, 59)
        time_layout.addWidget(self.time_second, 0, 5)

        time_layout.addWidget(QLabel("参数组合:"), 1, 0)
        self.time_combo = QComboBox()
        self.time_combo.addItems([
            "HOUR MINUTE SECOND (完整)",
            "HOUR MINUTE (时分)",
            "HOUR SECOND (时秒)",
            "MINUTE SECOND (分秒)",
            "HOUR (仅时)",
            "MINUTE (仅分)",
            "SECOND (仅秒)",
        ])
        time_layout.addWidget(self.time_combo, 1, 1, 1, 3)

        self.btn_time_set = QPushButton("设置时间")
        self.btn_time_get = QPushButton("获取时间")
        time_layout.addWidget(self.btn_time_set, 1, 4)
        time_layout.addWidget(self.btn_time_get, 1, 5)
        time_group.setLayout(time_layout)
        layout.addWidget(time_group)
        layout.addWidget(self._make_separator())

        # === 闹钟设置组 ===
        alarm_group = QGroupBox("闹钟  /  SET:ALARM")
        alarm_layout = QGridLayout()
        alarm_layout.addWidget(QLabel("时:"), 0, 0)
        self.alarm_hour = QSpinBox()
        self.alarm_hour.setRange(0, 23)
        alarm_layout.addWidget(self.alarm_hour, 0, 1)

        alarm_layout.addWidget(QLabel("分:"), 0, 2)
        self.alarm_minute = QSpinBox()
        self.alarm_minute.setRange(0, 59)
        alarm_layout.addWidget(self.alarm_minute, 0, 3)

        alarm_layout.addWidget(QLabel("秒:"), 0, 4)
        self.alarm_second = QSpinBox()
        self.alarm_second.setRange(0, 59)
        alarm_layout.addWidget(self.alarm_second, 0, 5)

        self.btn_alarm_set = QPushButton("设置闹钟")
        self.btn_alarm_get = QPushButton("获取闹钟")
        alarm_layout.addWidget(self.btn_alarm_set, 1, 1, 1, 2)
        alarm_layout.addWidget(self.btn_alarm_get, 1, 3, 1, 2)
        alarm_group.setLayout(alarm_layout)
        layout.addWidget(alarm_group)
        layout.addWidget(self._make_separator())

        # === 显示/格式组 ===
        disp_group = QGroupBox("显示策略  /  DISPLAY · FORMAT · MODE")
        disp_layout = QGridLayout()
        self.btn_disp_on = QPushButton("DISPLAY ON")
        self.btn_disp_off = QPushButton("DISPLAY OFF")
        self.btn_format_left = QPushButton("FORMAT LEFT")
        self.btn_format_right = QPushButton("FORMAT RIGHT")
        self.btn_mode_day = QPushButton("MODE DAY")
        self.btn_mode_night = QPushButton("MODE NIGHT")
        self.btn_get_display = QPushButton("获取显示")
        self.btn_get_format = QPushButton("获取格式")
        display_buttons = [
            self.btn_disp_on, self.btn_disp_off,
            self.btn_format_left, self.btn_format_right,
            self.btn_mode_day, self.btn_mode_night,
            self.btn_get_display, self.btn_get_format,
        ]
        for index, button in enumerate(display_buttons):
            disp_layout.addWidget(button, index // 4, index % 4)
        disp_group.setLayout(disp_layout)
        layout.addWidget(disp_group)
        layout.addWidget(self._make_separator())

        # === 消息 & LED 组 ===
        msg_led_group = QGroupBox("消息与 LED  /  OUTPUT")
        ml_layout = QHBoxLayout()
        ml_layout.addWidget(QLabel("消息 (≤32):"))
        self.msg_input = QLineEdit()
        self.msg_input.setMaxLength(32)
        self.msg_input.setPlaceholderText("输入最多 32 个字符…")
        ml_layout.addWidget(self.msg_input)
        self.btn_msg_send = QPushButton("发送消息")
        ml_layout.addWidget(self.btn_msg_send)

        ml_layout.addWidget(QLabel("LED (hex):"))
        self.led_input = QLineEdit()
        self.led_input.setMaxLength(2)
        self.led_input.setFixedWidth(40)
        self.led_input.setPlaceholderText("FF")
        ml_layout.addWidget(self.led_input)
        self.btn_led_set = QPushButton("设置LED")
        self.btn_led_default = QPushButton("恢复默认")
        ml_layout.addWidget(self.btn_led_set)
        ml_layout.addWidget(self.btn_led_default)
        msg_led_group.setLayout(ml_layout)
        layout.addWidget(msg_led_group)
        layout.addWidget(self._make_separator())

        # === 电机控制组 ===
        motor_group = QGroupBox("步进电机  /  MOTION")
        motor_layout = QHBoxLayout()
        self.btn_motor_start = QPushButton("START")
        self.btn_motor_stop = QPushButton("STOP")
        self.btn_motor_fwd = QPushButton("FWD")
        self.btn_motor_rev = QPushButton("REV")
        self.btn_motor_get = QPushButton("GET STATUS")
        motor_layout.addWidget(self.btn_motor_start)
        motor_layout.addWidget(self.btn_motor_stop)
        motor_layout.addWidget(self.btn_motor_fwd)
        motor_layout.addWidget(self.btn_motor_rev)
        motor_layout.addWidget(self.btn_motor_get)
        motor_group.setLayout(motor_layout)
        layout.addWidget(motor_group)
        layout.addWidget(self._make_separator())

        # === 系统命令 & 演示组 ===
        sys_group = QGroupBox("系统诊断  /  SYSTEM")
        sys_layout = QHBoxLayout()
        self.btn_rst = QPushButton("*RST (复位协议)")
        self.btn_init = QPushButton("INIT (系统复位)")
        self.btn_help = QPushButton("HELP")
        self.btn_ping = QPushButton("PING")
        sys_layout.addWidget(self.btn_rst)
        sys_layout.addWidget(self.btn_init)
        sys_layout.addWidget(self.btn_help)
        sys_layout.addWidget(self.btn_ping)

        sys_group.setLayout(sys_layout)
        layout.addWidget(sys_group)
        layout.addWidget(self._make_separator())

        demo_layout = QHBoxLayout()
        self.btn_demo_abbrev = QPushButton("缩写演示 (min-match)")
        self.btn_demo_case = QPushButton("大小写混合演示")
        demo_layout.addWidget(self.btn_demo_abbrev)
        demo_layout.addWidget(self.btn_demo_case)
        layout.addLayout(demo_layout)

        layout.addStretch()
        scroll.setWidget(content)
        outer_layout.addWidget(scroll)

    # ── 信号连接 ──

    def _connect_signals(self):
        # 日期
        self.btn_date_set.clicked.connect(self._on_date_set)
        self.btn_date_get.clicked.connect(self._on_date_get)

        # 时间
        self.btn_time_set.clicked.connect(self._on_time_set)
        self.btn_time_get.clicked.connect(self._on_time_get)

        # 闹钟
        self.btn_alarm_set.clicked.connect(self._on_alarm_set)
        self.btn_alarm_get.clicked.connect(self._on_alarm_get)

        # 显示/格式/模式
        self.btn_disp_on.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_set_display(True)))
        self.btn_disp_off.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_set_display(False)))
        self.btn_format_left.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_set_format(True)))
        self.btn_format_right.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_set_format(False)))
        self.btn_mode_day.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_set_mode(False)))
        self.btn_mode_night.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_set_mode(True)))
        self.btn_get_display.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_get_display()))
        self.btn_get_format.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_get_format()))

        # 消息/LED
        self.btn_msg_send.clicked.connect(self._on_msg_send)
        self.btn_led_set.clicked.connect(self._on_led_set)
        self.btn_led_default.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_set_led(0x00)))

        # 电机
        self.btn_motor_start.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_motor_start()))
        self.btn_motor_stop.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_motor_stop()))
        self.btn_motor_fwd.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_motor_fwd()))
        self.btn_motor_rev.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_motor_rev()))
        self.btn_motor_get.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_get_motor()))

        # 系统
        self.btn_rst.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_rst()))
        self.btn_init.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_init()))
        self.btn_help.clicked.connect(lambda: self.send_command.emit(
            self.protocol.build_help()))
        self.btn_ping.clicked.connect(lambda: self.send_command.emit("*PING"))

        # 演示
        self.btn_demo_abbrev.clicked.connect(self._on_demo_abbrev)
        self.btn_demo_case.clicked.connect(self._on_demo_case)

    # ── 发送处理 ──

    def _on_date_set(self):
        combo_idx = self.date_combo.currentIndex()
        y = self.date_year.value()
        m = self.date_month.value()
        d = self.date_day.value()

        # 根据下拉选择构造不同参数组合
        if combo_idx == 0:  # YEAR MONTH DATE
            cmd = self.protocol.build_set_date(year=y, month=m, day=d)
        elif combo_idx == 1:  # YEAR MONTH
            cmd = self.protocol.build_set_date(year=y, month=m)
        elif combo_idx == 2:  # YEAR DATE
            cmd = self.protocol.build_set_date(year=y, day=d)
        elif combo_idx == 3:  # MONTH DATE
            cmd = self.protocol.build_set_date(month=m, day=d)
        elif combo_idx == 4:  # YEAR
            cmd = self.protocol.build_set_date(year=y)
        elif combo_idx == 5:  # MONTH
            cmd = self.protocol.build_set_date(month=m)
        else:  # DATE
            cmd = self.protocol.build_set_date(day=d)

        self.send_command.emit(cmd)

    def _on_date_get(self):
        self.send_command.emit(self.protocol.build_get_date())

    def _on_time_set(self):
        combo_idx = self.time_combo.currentIndex()
        h = self.time_hour.value()
        m = self.time_minute.value()
        s = self.time_second.value()

        if combo_idx == 0:
            cmd = self.protocol.build_set_time(hour=h, minute=m, second=s)
        elif combo_idx == 1:
            cmd = self.protocol.build_set_time(hour=h, minute=m)
        elif combo_idx == 2:
            cmd = self.protocol.build_set_time(hour=h, second=s)
        elif combo_idx == 3:
            cmd = self.protocol.build_set_time(minute=m, second=s)
        elif combo_idx == 4:
            cmd = self.protocol.build_set_time(hour=h)
        elif combo_idx == 5:
            cmd = self.protocol.build_set_time(minute=m)
        else:
            cmd = self.protocol.build_set_time(second=s)

        self.send_command.emit(cmd)

    def _on_time_get(self):
        self.send_command.emit(self.protocol.build_get_time())

    def _on_alarm_set(self):
        h = self.alarm_hour.value()
        m = self.alarm_minute.value()
        s = self.alarm_second.value()
        cmd = self.protocol.build_set_alarm(hour=h, minute=m, second=s)
        self.send_command.emit(cmd)

    def _on_alarm_get(self):
        self.send_command.emit(self.protocol.build_get_alarm())

    def _on_msg_send(self):
        text = self.msg_input.text().strip()
        if text:
            self.send_command.emit(self.protocol.build_set_msg(text))

    def _on_led_set(self):
        text = self.led_input.text().strip()
        if text:
            try:
                val = int(text, 16)
                self.send_command.emit(self.protocol.build_set_led(val))
            except ValueError:
                pass

    # ── 演示按钮 ──

    def _on_demo_abbrev(self):
        """发送最小匹配缩写命令

        MCU 命令解析器支持最小匹配 (min-match)：只要前缀能唯一确定一个 token 即可。
        例如 "DAT" 匹配 "DATE", "YEA" 匹配 "YEAR", "MON" 匹配 "MONTH"。
        实际使用时建议发送完整 token，仅测试时使用缩写。
        """
        self.send_command.emit("*SET:DAT YEA MON 2025 6 15")

    def _on_demo_case(self):
        """发送大小写混合命令

        MCU 命令解析器对大小写不敏感 (case-insensitive)。
        例如 "*SeT:DaTe" 与 "*SET:DATE" 效果完全相同。
        实际使用时建议使用标准大写格式。
        """
        self.send_command.emit("*SeT:DaTe YeAr MoNtH dAtE 2025 6 15")

    # ── 样式 ──

    @staticmethod
    def _make_separator() -> QFrame:
        """创建分组之间的细分割线"""
        line = QFrame()
        line.setFrameShape(QFrame.HLine)
        line.setFrameShadow(QFrame.Sunken)
        line.setObjectName("group_separator")
        return line

    def _apply_styles(self):
        """通过动态属性声明按钮语义，具体视觉统一交给 theme.qss。"""
        primary_buttons = (
            self.btn_date_set, self.btn_time_set, self.btn_alarm_set,
            self.btn_msg_send, self.btn_led_set, self.btn_motor_start,
        )
        for button in primary_buttons:
            button.setProperty("primary", True)

        secondary_buttons = (
            self.btn_date_get, self.btn_time_get, self.btn_alarm_get,
            self.btn_get_display, self.btn_get_format, self.btn_motor_get,
        )
        for button in secondary_buttons:
            button.setProperty("quiet", True)

        self.btn_motor_stop.setProperty("danger", True)
        self.btn_init.setProperty("danger", True)
        self.btn_rst.setProperty("warning", True)

        for button in (self.btn_demo_abbrev, self.btn_demo_case):
            button.setProperty("demo", True)

    # ── 控件使能控制 ──

    def set_controls_enabled(self, enabled: bool):
        """连接/断开时启用/禁用控件"""
        for child in self.findChildren(QPushButton):
            # 保留 COM 扫描相关按钮始终可用 (TODO: 细化)
            child.setEnabled(enabled)
