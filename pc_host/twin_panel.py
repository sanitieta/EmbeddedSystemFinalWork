"""
Digital Twin Mirror Panel
==========================
Software simulation of the S800 board: 8-digit 7-segment display, 8 LEDs,
8 matrix keys (K1-K8), and 2 independent user keys (USER1/USER2).

Bidirectional sync with MCU firmware over the UART ASCII protocol:
- *EVT:DISP  -> update 7SEG display (characters and decimal point bitmap)
- *EVT:LED   -> update LED on/off state
- *EVT:MODE  -> update panel rendering mode (DAY / NIGHT)
- *EVT:KEY   -> highlight the corresponding virtual key
- Virtual key click -> emit "*SET:KEY <NAME>" for serial delivery to MCU

Format RIGHT rules:
- 8-character display string is reversed.
- Decimal-point bitmap is reversed (bit N -> bit 6-N).

Night Mode:
- Only digit positions 0-3 (HH.MM) are visible; positions 4-7 are blanked.
- Only LED0 (heartbeat) remains lit; LEDs 1-7 are forced off.
"""

from __future__ import annotations

from typing import ClassVar

from PyQt5.QtCore import (
    Qt,
    QPointF,
    QRectF,
    QTimer,
    pyqtSignal,
)
from PyQt5.QtGui import (
    QBrush,
    QColor,
    QFont,
    QPainter,
    QPainterPath,
    QRadialGradient,
    QPaintEvent,
)
from PyQt5.QtWidgets import (
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QSizePolicy,
    QVBoxLayout,
    QWidget,
)


# ---------------------------------------------------------------------------
# Segment geometry
# ---------------------------------------------------------------------------
# Each segment is a trapezoid polygon defined in a unit-square coordinate
# system (0.0-1.0).  The painter applies translate + scale to map these
# coordinates onto the actual widget pixel area.
#
# Segment bit mapping (standard 7-seg, bit 0 = A):
#
#        AAAA
#       F    B
#       F    B
#        GGGG
#       E    C
#       E    C
#        DDDD   . dp
#
# A: top horizontal      B: top-right vertical
# C: bottom-right vert   D: bottom horizontal
# E: bottom-left vert    F: top-left vertical
# G: middle horizontal

_SEGMENT_POINTS: dict[str, list[tuple[float, float]]] = {
    "A": [(0.10, 0.05), (0.90, 0.05), (0.85, 0.10), (0.15, 0.10)],
    "B": [(0.90, 0.10), (0.95, 0.45), (0.90, 0.50), (0.85, 0.45), (0.85, 0.15)],
    "C": [(0.90, 0.55), (0.95, 0.90), (0.90, 0.95), (0.85, 0.90), (0.85, 0.60)],
    "D": [(0.10, 0.90), (0.90, 0.90), (0.85, 0.95), (0.15, 0.95)],
    "E": [
        (0.05, 0.55),
        (0.10, 0.50),
        (0.15, 0.55),
        (0.15, 0.90),
        (0.10, 0.95),
        (0.05, 0.90),
    ],
    "F": [
        (0.05, 0.10),
        (0.10, 0.05),
        (0.15, 0.10),
        (0.15, 0.45),
        (0.10, 0.50),
        (0.05, 0.45),
    ],
    "G": [(0.10, 0.48), (0.90, 0.48), (0.85, 0.53), (0.15, 0.53)],
}

# Bit -> segment name lookup
_SEGMENT_BIT_TO_NAME: dict[int, str] = {
    0x01: "A",  # bit 0
    0x02: "B",  # bit 1
    0x04: "C",  # bit 2
    0x08: "D",  # bit 3
    0x10: "E",  # bit 4
    0x20: "F",  # bit 5
    0x40: "G",  # bit 6
}

# Colours
_COLOR_ON: QColor = QColor("#FF3030")   # bright red
_COLOR_OFF: QColor = QColor("#220000")   # dim dark red (visible but clearly off)
_COLOR_BG: QColor = QColor("#0A0A0A")   # near-black background

# Decimal-point centre in unit-square coordinates
_DP_CENTER: QPointF = QPointF(0.92, 0.88)
_DP_RX: float = 0.06
_DP_RY: float = 0.08


# ---------------------------------------------------------------------------
# 7-segment display character code table (standard common-anode mapping)
# ---------------------------------------------------------------------------

_ASCII_TO_7SEG: dict[str, int] = {
    "0": 0x3F,
    "1": 0x06,
    "2": 0x5B,
    "3": 0x4F,
    "4": 0x66,
    "5": 0x6D,
    "6": 0x7D,
    "7": 0x07,
    "8": 0x7F,
    "9": 0x6F,
    "A": 0x77,
    "B": 0x7C,
    "C": 0x39,
    "D": 0x5E,
    "E": 0x79,
    "F": 0x71,
    "G": 0x3D,
    "H": 0x76,
    "I": 0x06,
    "J": 0x1E,
    "L": 0x38,
    "N": 0x54,
    "O": 0x3F,
    "P": 0x73,
    "R": 0x50,
    "S": 0x6D,
    "T": 0x78,
    "U": 0x3E,
    "X": 0x76,
    "Y": 0x6E,
    "-": 0x40,
    "_": 0x08,
    "=": 0x48,
    " ": 0x00,
}


# ---------------------------------------------------------------------------
# SevenSegWidget — single 7-segment digit (plus decimal point)
# ---------------------------------------------------------------------------

class SevenSegWidget(QWidget):
    """A single 7-segment digit drawn with QPainterPath trapezoid polygons.

    Public API:
        set_segments(byte: int)  -- accept 0x00-0x7F segment bitmap
        set_dp(on: bool)         -- control decimal point visibility
    """

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._segments: int = 0x00
        self._dp: bool = False
        self.setMinimumSize(50, 80)
        self.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)

    # -- public API --------------------------------------------------------

    def set_segments(self, seg_byte: int) -> None:
        """Set the 7-segment code (0x00-0x7F).

        Bit 0 = A (top), bit 1 = B (top-right), bit 2 = C (bottom-right),
        bit 3 = D (bottom), bit 4 = E (bottom-left), bit 5 = F (top-left),
        bit 6 = G (middle).
        """
        self._segments = seg_byte & 0x7F
        self.update()

    def set_dp(self, on: bool) -> None:
        """Turn the decimal point on or off."""
        self._dp = on
        self.update()

    # -- paint -------------------------------------------------------------

    def paintEvent(self, event: QPaintEvent) -> None:  # noqa: N803
        """Draw all seven segments and the decimal point."""
        w: int = self.width()
        h: int = self.height()

        margin: int = 4
        seg_w: float = float(w - 2 * margin)
        seg_h: float = float(h - 2 * margin)

        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)

        # Background
        painter.fillRect(self.rect(), _COLOR_BG)

        # Scale to unit square
        painter.save()
        painter.translate(margin, margin)
        painter.scale(seg_w, seg_h)

        painter.setPen(Qt.NoPen)

        for bit_val, name in _SEGMENT_BIT_TO_NAME.items():
            on: bool = bool(self._segments & bit_val)
            color: QColor = _COLOR_ON if on else _COLOR_OFF
            painter.setBrush(QBrush(color))

            pts: list[tuple[float, float]] = _SEGMENT_POINTS.get(name, [])
            if not pts:
                continue

            path = QPainterPath()
            path.moveTo(QPointF(*pts[0]))
            for pt in pts[1:]:
                path.lineTo(QPointF(*pt))
            path.closeSubpath()
            painter.drawPath(path)

        # Decimal point — small filled ellipse
        dp_color: QColor = _COLOR_ON if self._dp else _COLOR_OFF
        painter.setBrush(QBrush(dp_color))
        painter.drawEllipse(_DP_CENTER, _DP_RX, _DP_RY)

        painter.restore()


# ---------------------------------------------------------------------------
# LedIndicator — single LED circle with radial gradient
# ---------------------------------------------------------------------------

class LedIndicator(QWidget):
    """A single LED indicator rendered as a radial-gradient circle.

    Public API:
        set_on(on: bool)  -- turn the LED on or off
    """

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._on: bool = False
        self.setFixedSize(20, 20)

    def set_on(self, on: bool) -> None:
        """Set the LED state."""
        self._on = on
        self.update()

    def paintEvent(self, event: QPaintEvent) -> None:  # noqa: N803
        """Draw the LED as a radial-gradient filled circle."""
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)

        center: QPointF = QRectF(self.rect()).center()
        radius: float = min(self.width(), self.height()) / 2.0 - 2.0

        gradient = QRadialGradient(center, radius)
        if self._on:
            gradient.setColorAt(0.0, QColor("#00FF00"))
            gradient.setColorAt(0.6, QColor("#00CC00"))
            gradient.setColorAt(1.0, QColor("#003300"))
        else:
            gradient.setColorAt(0.0, QColor("#444444"))
            gradient.setColorAt(1.0, QColor("#111111"))

        painter.setBrush(QBrush(gradient))
        painter.setPen(Qt.NoPen)
        painter.drawEllipse(center, radius, radius)


# ---------------------------------------------------------------------------
# TwinPanel — full digital twin assembly
# ---------------------------------------------------------------------------

_KEY_NAMES: list[str] = [
    "FUNC", "SHIFT", "ADD", "SAVE",
    "DISP", "SPEED", "FORMAT", "EXT",
]


class TwinPanel(QWidget):
    """Digital twin mirror panel — complete S800 board simulation.

    Layout (top to bottom):
        1. Title label
        2. 8 x SevenSegWidget in an HBox
        3. 8 x LedIndicator in an HBox
        4. 4x2 grid of K1-K8 buttons
        5. USER1 / USER2 HBox

    Public signals:
        virtual_key_pressed(str)  -- emitted on any virtual key click

    Public methods:
        update_display(chars: list[str], dp: int)  -- from *EVT:DISP
        update_leds(pattern: int)                   -- from *EVT:LED
        update_mode(state: str)                     -- from *EVT:MODE
        highlight_key(name: str)                    -- from *EVT:KEY
    """

    virtual_key_pressed: pyqtSignal = pyqtSignal(str)

    # Button highlight duration (ms)
    _HIGHLIGHT_MS: ClassVar[int] = 200

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setWindowTitle("MCU Mirror (Digital Twin)")

        # Child widgets
        self.seg_widgets: list[SevenSegWidget] = []
        self.led_widgets: list[LedIndicator] = []
        self.key_buttons: list[QPushButton] = []

        self.user1_btn: QPushButton
        self.user2_btn: QPushButton

        # Internal state
        self._night_mode: bool = False
        self._display_on: bool = True

        # Active highlight timers (keyed by key name)
        self._highlight_timers: dict[str, QTimer] = {}

        self._build_ui()

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------

    def _build_ui(self) -> None:
        """Create and layout all child widgets."""
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(8, 8, 8, 8)
        main_layout.setSpacing(8)

        # -- Title --
        title = QLabel("MCU Mirror (Digital Twin)")
        title.setAlignment(Qt.AlignCenter)
        title_font = QFont("Microsoft YaHei", 12)
        title_font.setBold(True)
        title.setFont(title_font)
        main_layout.addWidget(title)

        # -- 7SEG row --
        seg_layout = QHBoxLayout()
        seg_layout.setSpacing(4)
        for _ in range(8):
            seg = SevenSegWidget(self)
            seg.setFixedSize(58, 85)
            self.seg_widgets.append(seg)
            seg_layout.addWidget(seg, alignment=Qt.AlignCenter)
        main_layout.addLayout(seg_layout)

        # -- LED row --
        led_layout = QHBoxLayout()
        led_layout.setSpacing(12)
        for _ in range(8):
            led = LedIndicator(self)
            self.led_widgets.append(led)
            led_layout.addWidget(led, alignment=Qt.AlignCenter)
        main_layout.addLayout(led_layout)

        # -- Matrix keys K1-K8 (4x2 grid) --
        key_grid = QGridLayout()
        key_grid.setSpacing(6)
        positions: list[tuple[int, int]] = [
            (0, 0), (0, 1), (0, 2), (0, 3),  # K1-K4
            (1, 0), (1, 1), (1, 2), (1, 3),  # K5-K8
        ]
        for i, (row, col) in enumerate(positions):
            name: str = _KEY_NAMES[i]
            btn = QPushButton(f"K{i + 1}\n{name}")
            btn.setMinimumSize(70, 50)
            btn.setFont(QFont("Microsoft YaHei", 8))
            # Lambda default-argument captures `name` at definition time
            btn.clicked.connect(lambda checked, n=name: self._on_key_clicked(n))
            key_grid.addWidget(btn, row, col)
            self.key_buttons.append(btn)
        main_layout.addLayout(key_grid)

        # -- USER1 / USER2 independent keys --
        user_layout = QHBoxLayout()
        user_layout.setSpacing(16)

        self.user1_btn = self._make_user_button("USER1\n(PJ0)")
        self.user1_btn.clicked.connect(lambda: self._on_key_clicked("USER1"))

        self.user2_btn = self._make_user_button("USER2\n(PJ1)")
        self.user2_btn.clicked.connect(lambda: self._on_key_clicked("USER2"))

        user_layout.addWidget(self.user1_btn)
        user_layout.addWidget(self.user2_btn)
        main_layout.addLayout(user_layout)

        main_layout.addStretch()

    # ------------------------------------------------------------------
    # Internal helpers for button creation
    # ------------------------------------------------------------------

    @staticmethod
    def _make_user_button(text: str) -> QPushButton:
        """Create a USER1/USER2 button with the standard blue styling."""
        btn = QPushButton(text)
        btn.setMinimumSize(140, 50)
        btn.setFont(QFont("Microsoft YaHei", 9, QFont.Bold))
        btn.setStyleSheet(
            "QPushButton {"
            "  background-color: #2A4A6B;"
            "  color: white;"
            "  border: 2px solid #4A8AD4;"
            "  border-radius: 6px;"
            "}"
            "QPushButton:hover {"
            "  background-color: #3A5A7B;"
            "}"
        )
        return btn

    # ------------------------------------------------------------------
    # Public API — called from the main window when MCU events arrive
    # ------------------------------------------------------------------

    def update_display(self, chars: list[str], dp: int) -> None:
        """Update all 8 seven-segment digits from an *EVT:DISP event.

        Args:
            chars: Exactly 8 display characters (e.g. ['1','2','.','3','0','.','4','5']).
                   The decimal point is NOT included as a character.
            dp: Two-digit hex bitmap (bit N = decimal point for digit N).
        """
        self._display_on = True
        for i in range(8):
            if i < len(chars):
                seg_byte: int = self._char_to_segments(chars[i])
                self.seg_widgets[i].set_segments(seg_byte)
                self.seg_widgets[i].set_dp(bool(dp & (1 << i)))
            else:
                self.seg_widgets[i].set_segments(0x00)
                self.seg_widgets[i].set_dp(False)

        # Night-mode masking is applied on top if active
        if self._night_mode:
            self._apply_night_mask()

    def update_leds(self, pattern: int) -> None:
        """Update all 8 LEDs from an *EVT:LED event.

        Args:
            pattern: Byte where bit N controls LED N (0 = off, 1 = on).
        """
        for i in range(8):
            self.led_widgets[i].set_on(bool(pattern & (1 << i)))

        if self._night_mode:
            self._apply_night_mask()

    def update_mode(self, state: str) -> None:
        """Update the panel rendering mode from an *EVT:MODE event.

        Args:
            state: "NIGHT" to enter night mode, anything else (e.g. "DAY",
                   "FLOWING", "DATE", "TIME", "ALARM") to restore day mode.
        """
        if state == "NIGHT":
            self._night_mode = True
            self._apply_night_mask()
        else:
            self._night_mode = False

    def highlight_key(self, name: str) -> None:
        """Flash a virtual key button gold for 200 ms.

        Args:
            name: One of the key names: FUNC, SHIFT, ADD, SAVE, DISP, SPEED,
                  FORMAT, EXT, USER1, USER2.
        """
        btn: QPushButton | None = self._find_button(name)
        if btn is None:
            return

        # Cancel any still-running highlight timer for this key
        if name in self._highlight_timers:
            self._highlight_timers[name].stop()

        original_style: str = btn.styleSheet()
        btn.setStyleSheet(
            original_style
            + " QPushButton {"
            "  background-color: #FFD700;"
            "  border: 2px solid #FFA500;"
            "}"
        )

        def restore() -> None:
            btn.setStyleSheet(original_style)
            self._highlight_timers.pop(name, None)

        timer = QTimer(self)
        timer.setSingleShot(True)
        timer.timeout.connect(restore)
        self._highlight_timers[name] = timer
        timer.start(self._HIGHLIGHT_MS)

    # ------------------------------------------------------------------
    # Private helpers
    # ------------------------------------------------------------------

    def _on_key_clicked(self, name: str) -> None:
        """Handle a virtual key click.

        Emit the *SET:KEY command string and visually highlight the key.
        """
        self.virtual_key_pressed.emit(f"*SET:KEY {name}")
        self.highlight_key(name)

    def _find_button(self, name: str) -> QPushButton | None:
        """Look up a QPushButton by key name."""
        if name in _KEY_NAMES:
            idx: int = _KEY_NAMES.index(name)
            if idx < len(self.key_buttons):
                return self.key_buttons[idx]
        if name == "USER1":
            return self.user1_btn
        if name == "USER2":
            return self.user2_btn
        return None

    def _apply_night_mask(self) -> None:
        """Blank digits 4-7 and LEDs 1-7 for night mode."""
        for i in range(4, 8):
            self.seg_widgets[i].set_segments(0x00)
            self.seg_widgets[i].set_dp(False)
        for i in range(1, 8):
            self.led_widgets[i].set_on(False)

    @staticmethod
    def _char_to_segments(ch: str) -> int:
        """Convert an ASCII character to its 7-segment code.

        Returns 0x00 for unrecognised characters.
        """
        ch_upper: str = ch.upper()
        return _ASCII_TO_7SEG.get(ch_upper, 0x00)
