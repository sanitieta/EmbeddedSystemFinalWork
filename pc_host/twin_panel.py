"""
数字孪生镜像面板
================
软件仿真 S800 板的 8 位 7SEG + 8 位 LED + 8 按键 + 2 独立按键，
与 MCU 双向同步:

- *EVT:DISP → 更新 7SEG 显示 (含小数点)
- *EVT:LED  → 更新 LED 亮灭
- 点击虚拟按键 → *SET:KEY <NAME>
- *EVT:MODE NIGHT → 仅显示 4 位时分 + LED0 心跳
- FORMAT RIGHT → 字符反转 + DP 位图反转
"""

from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QPushButton, QLabel, QFrame, QSizePolicy,
)
from PyQt5.QtCore import (
    Qt, QTimer, pyqtSignal, QRectF, QPointF,
)
from PyQt5.QtGui import (
    QPainter, QPainterPath, QColor, QPen, QBrush,
    QFont, QPolygonF, QRadialGradient, QPaintEvent,
)


# ═══════════════════════════════════════════════════════════════
# 7 段数码管自绘 Widget
# ═══════════════════════════════════════════════════════════════

# 段定义: 每个段是一条从起点到终点的多边形 (相对坐标 0.0–1.0)
# 标准 7 段布局:
#      AAAA
#     F    B
#     F    B
#      GGGG
#     E    C
#     E    C
#      DDDD   ● dp

# 每个段的顶点坐标 (相对于单位正方形)
_SEGMENTS = {
    "A": [(0.10, 0.05), (0.90, 0.05), (0.85, 0.10), (0.15, 0.10)],
    "B": [(0.90, 0.10), (0.95, 0.45), (0.90, 0.50), (0.85, 0.45), (0.85, 0.15)],
    "C": [(0.90, 0.55), (0.95, 0.90), (0.90, 0.95), (0.85, 0.90), (0.85, 0.60)],
    "D": [(0.10, 0.90), (0.90, 0.90), (0.85, 0.95), (0.15, 0.95)],
    "E": [(0.05, 0.55), (0.10, 0.50), (0.15, 0.55), (0.15, 0.90), (0.10, 0.95), (0.05, 0.90)],
    "F": [(0.05, 0.10), (0.10, 0.05), (0.15, 0.10), (0.15, 0.45), (0.10, 0.50), (0.05, 0.45)],
    "G": [(0.10, 0.48), (0.90, 0.48), (0.85, 0.53), (0.15, 0.53)],
}

# 7 段码 → 段名映射 (标准共阳)
# seg7[0] = 0x3F = 0b00111111 → A,B,C,D,E,F
# seg7[1] = 0x06 = 0b00000110 → B,C
# ...
_SEG_MAP = {
    0x01: "A", 0x02: "B", 0x04: "C", 0x08: "D",
    0x10: "E", 0x20: "F", 0x40: "G",
}

COLOR_ON = QColor("#FF3030")       # 亮红色
COLOR_OFF = QColor("#220000")      # 暗红色 (可见但明显区分)
COLOR_BG = QColor("#0A0A0A")       # 背景


class SevenSegWidget(QWidget):
    """单个 7 段数码管 (含小数点)"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._segments = 0x00  # 7 段码 (bit 0-6 = A-G)
        self._dp = False       # 小数点
        self.setMinimumSize(50, 80)
        self.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)

    def set_segments(self, seg_byte: int):
        """设置 7 段码 (0x00–0x7F)"""
        self._segments = seg_byte & 0x7F
        self.update()

    def set_dp(self, on: bool):
        """设置小数点亮灭"""
        self._dp = on
        self.update()

    def paintEvent(self, event: QPaintEvent):
        w = self.width()
        h = self.height()
        # 留出边距
        margin = 4
        seg_w = w - 2 * margin
        seg_h = h - 2 * margin

        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)

        # 背景
        painter.fillRect(self.rect(), COLOR_BG)

        # 缩放到实际像素
        painter.save()
        painter.translate(margin, margin)
        painter.scale(seg_w, seg_h)

        for bit, name in _SEG_MAP.items():
            on = bool(self._segments & bit)
            color = COLOR_ON if on else COLOR_OFF
            painter.setBrush(QBrush(color))
            painter.setPen(Qt.NoPen)

            pts = _SEGMENTS.get(name, [])
            if pts:
                poly = QPolygonF([QPointF(x, y) for x, y in pts])
                painter.drawPolygon(poly)

        # 小数点
        painter.setBrush(QBrush(COLOR_ON if self._dp else COLOR_OFF))
        painter.drawEllipse(QPointF(0.92, 0.88), 0.06, 0.08)

        painter.restore()


# ═══════════════════════════════════════════════════════════════
# LED 指示灯 Widget
# ═══════════════════════════════════════════════════════════════

class LedIndicator(QWidget):
    """单个 LED 指示灯 (QSS 径向渐变)"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._on = False
        self.setFixedSize(20, 20)

    def set_on(self, on: bool):
        self._on = on
        self.update()

    def paintEvent(self, event: QPaintEvent):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)

        center = self.rect().center()
        radius = min(self.width(), self.height()) / 2 - 2

        if self._on:
            gradient = QRadialGradient(center, radius)
            gradient.setColorAt(0.0, QColor("#00FF00"))
            gradient.setColorAt(0.6, QColor("#00CC00"))
            gradient.setColorAt(1.0, QColor("#003300"))
        else:
            gradient = QRadialGradient(center, radius)
            gradient.setColorAt(0.0, QColor("#444444"))
            gradient.setColorAt(1.0, QColor("#111111"))

        painter.setBrush(QBrush(gradient))
        painter.setPen(Qt.NoPen)
        painter.drawEllipse(center, radius, radius)


# ═══════════════════════════════════════════════════════════════
# 数字孪生面板 (组合)
# ═══════════════════════════════════════════════════════════════

KEY_NAMES = ["FUNC", "SHIFT", "ADD", "SAVE", "DISP", "SPEED", "FORMAT", "EXT"]


class TwinPanel(QWidget):
    """数字孪生镜像面板 — 完整 S800 板面仿真"""

    virtual_key_pressed = pyqtSignal(str)  # 发送 "*SET:KEY <NAME>"

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("MCU 镜像 (Digital Twin)")

        # 子控件
        self.seg_widgets: list[SevenSegWidget] = []
        self.led_widgets: list[LedIndicator] = []
        self.key_buttons: list[QPushButton] = []
        self.user1_btn: QPushButton | None = None
        self.user2_btn: QPushButton | None = None

        # 状态
        self._night_mode = False
        self._display_on = True

        self._build_ui()

        # 按键高亮定时器 (200ms)
        self._highlight_timers: dict[str, QTimer] = {}

    def _build_ui(self):
        main_layout = QVBoxLayout(self)

        # ---- 标题 ----
        title = QLabel("MCU 镜像 (Digital Twin)")
        title.setAlignment(Qt.AlignCenter)
        title.setFont(QFont("Microsoft YaHei", 12, QFont.Bold))
        main_layout.addWidget(title)

        # ---- 7SEG 行 ----
        seg_layout = QHBoxLayout()
        seg_layout.setSpacing(4)
        for i in range(8):
            seg = SevenSegWidget(self)
            seg.setFixedSize(58, 85)
            self.seg_widgets.append(seg)
            seg_layout.addWidget(seg, alignment=Qt.AlignCenter)
        main_layout.addLayout(seg_layout)

        # ---- LED 行 ----
        led_layout = QHBoxLayout()
        led_layout.setSpacing(12)
        for i in range(8):
            led = LedIndicator(self)
            self.led_widgets.append(led)
            led_layout.addWidget(led, alignment=Qt.AlignCenter)
        main_layout.addLayout(led_layout)

        # ---- 矩阵按键 K1-K8 (4×2 网格) ----
        key_grid = QGridLayout()
        key_grid.setSpacing(6)
        positions = [
            (0, 0), (0, 1), (0, 2), (0, 3),  # K1-K4
            (1, 0), (1, 1), (1, 2), (1, 3),  # K5-K8
        ]
        for i, (row, col) in enumerate(positions):
            btn = QPushButton(f"K{i+1}\n{KEY_NAMES[i]}")
            btn.setMinimumSize(70, 50)
            btn.setFont(QFont("Microsoft YaHei", 8))
            btn.clicked.connect(lambda checked, n=KEY_NAMES[i]: self._on_key_clicked(n))
            key_grid.addWidget(btn, row, col)
            self.key_buttons.append(btn)
        main_layout.addLayout(key_grid)

        # ---- USER1 / USER2 独立按键 ----
        user_layout = QHBoxLayout()
        self.user1_btn = QPushButton("USER1\n(PJ0)")
        self.user1_btn.setMinimumSize(140, 50)
        self.user1_btn.setFont(QFont("Microsoft YaHei", 9, QFont.Bold))
        self.user1_btn.setStyleSheet(
            "QPushButton { background-color: #2A4A6B; color: white; border: 2px solid #4A8AD4; border-radius: 6px; }"
            "QPushButton:hover { background-color: #3A5A7B; }"
        )
        self.user1_btn.clicked.connect(lambda: self._on_key_clicked("USER1"))

        self.user2_btn = QPushButton("USER2\n(PJ1)")
        self.user2_btn.setMinimumSize(140, 50)
        self.user2_btn.setFont(QFont("Microsoft YaHei", 9, QFont.Bold))
        self.user2_btn.setStyleSheet(
            "QPushButton { background-color: #2A4A6B; color: white; border: 2px solid #4A8AD4; border-radius: 6px; }"
            "QPushButton:hover { background-color: #3A5A7B; }"
        )
        self.user2_btn.clicked.connect(lambda: self._on_key_clicked("USER2"))

        user_layout.addWidget(self.user1_btn)
        user_layout.addWidget(self.user2_btn)
        main_layout.addLayout(user_layout)

        main_layout.addStretch()

    # ---- Public update methods (called from main window) ----

    def update_display(self, chars: list[str], dp: int):
        """根据 *EVT:DISP 更新 7SEG 显示"""
        self._display_on = True
        for i in range(8):
            if i < len(chars):
                seg_byte = self._char_to_segments(chars[i])
                self.seg_widgets[i].set_segments(seg_byte)
                self.seg_widgets[i].set_dp(bool(dp & (1 << i)))
            else:
                self.seg_widgets[i].set_segments(0x00)
                self.seg_widgets[i].set_dp(False)

    def update_leds(self, pattern: int):
        """根据 *EVT:LED <hex2> 更新 LED 亮灭"""
        for i in range(8):
            self.led_widgets[i].set_on(bool(pattern & (1 << i)))

    def update_mode(self, state: str):
        """根据 *EVT:MODE 更新面板渲染模式"""
        if state == "NIGHT":
            self._night_mode = True
            self._apply_night_mode()
        elif state == "DAY":
            self._night_mode = False
            self._restore_day_mode()

    def highlight_key(self, name: str):
        """收到 *EVT:KEY 后高亮对应按键 200ms"""
        btn = self._find_button(name)
        if btn is None:
            return

        original_style = btn.styleSheet()
        btn.setStyleSheet(
            original_style
            + " QPushButton { background-color: #FFD700; border: 2px solid #FFA500; }"
        )

        # 取消之前的定时器
        if name in self._highlight_timers:
            self._highlight_timers[name].stop()

        timer = QTimer(self)
        timer.setSingleShot(True)
        timer.timeout.connect(lambda: self._restore_button_style(btn, original_style, name))
        self._highlight_timers[name] = timer
        timer.start(200)

    # ---- Internal helpers ----

    def _on_key_clicked(self, name: str):
        """虚拟按键点击 → 发出 *SET:KEY 命令"""
        self.virtual_key_pressed.emit(f"*SET:KEY {name}")
        # 同时也高亮自身
        self.highlight_key(name)

    def _find_button(self, name: str) -> QPushButton | None:
        if name in KEY_NAMES:
            idx = KEY_NAMES.index(name)
            return self.key_buttons[idx]
        if name == "USER1":
            return self.user1_btn
        if name == "USER2":
            return self.user2_btn
        return None

    def _restore_button_style(self, btn: QPushButton, style: str, name: str):
        btn.setStyleSheet(style)
        if name in self._highlight_timers:
            del self._highlight_timers[name]

    def _apply_night_mode(self):
        """夜间模式: 仅显示 4 位时分 + LED0 心跳"""
        # 只留前 4 位 (HH.MM)
        for i in range(4, 8):
            self.seg_widgets[i].set_segments(0x00)
            self.seg_widgets[i].set_dp(False)
        # 仅 LED0 亮
        for i in range(1, 8):
            self.led_widgets[i].set_on(False)

    def _restore_day_mode(self):
        """日间模式: 恢复完整 8 位 + 全部 LED"""
        pass  # 等待下一次 *EVT:DISP / *EVT:LED 刷新

    @staticmethod
    def _char_to_segments(ch: str) -> int:
        """字符 → 7 段码 (标准共阳码表)"""
        mapping = {
            '0': 0x3F, '1': 0x06, '2': 0x5B, '3': 0x4F,
            '4': 0x66, '5': 0x6D, '6': 0x7D, '7': 0x07,
            '8': 0x7F, '9': 0x6F,
            'A': 0x77, 'B': 0x7C, 'C': 0x39, 'D': 0x5E,
            'E': 0x79, 'F': 0x71,
            '-': 0x40, '_': 0x00, ' ': 0x00,
            'x': 0x00, 'X': 0x76,
            'H': 0x76, 'L': 0x38, 'P': 0x73,
        }
        ch_upper = ch.upper()
        return mapping.get(ch_upper, 0x00)
