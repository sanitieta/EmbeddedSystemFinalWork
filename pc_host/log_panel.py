"""
收发日志面板
============
- QPlainTextEdit, 最多 1000 行
- 格式: [HH:MM:SS.fff] →/← 内容
- 颜色编码: TX 蓝、RX 绿、事件紫、错误红
- 可导出到文件
"""

from datetime import datetime
from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout,
    QPlainTextEdit, QPushButton, QLabel, QFileDialog, QCheckBox,
)
from PyQt5.QtCore import Qt
from PyQt5.QtGui import QTextCursor, QColor, QTextCharFormat, QFont


MAX_LINES = 1000
EXPORT_DIR = "logs"


class LogPanel(QWidget):
    """收发日志面板"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("收发日志")

        layout = QVBoxLayout(self)

        # 标题行
        header = QHBoxLayout()
        header.addWidget(QLabel("收发日志"))
        self.chk_newest_first = QCheckBox("最新在上")
        self.chk_newest_first.setChecked(True)
        header.addWidget(self.chk_newest_first)
        header.addStretch()
        self.btn_clear = QPushButton("清空")
        self.btn_export = QPushButton("导出")
        header.addWidget(self.btn_clear)
        header.addWidget(self.btn_export)
        layout.addLayout(header)

        # 日志编辑区
        self.log_edit = QPlainTextEdit()
        self.log_edit.setReadOnly(True)
        self.log_edit.setMaximumBlockCount(MAX_LINES)
        self.log_edit.setFont(QFont("Consolas", 10))
        layout.addWidget(self.log_edit)

        # 应用样式
        self._apply_log_styles()

        # 连接信号
        self.btn_clear.clicked.connect(self.log_edit.clear)
        self.btn_export.clicked.connect(self._export_log)

    # ── 公开方法 ──

    # 调色板一致的颜色常量 (基于 Fusion dark 主题的和谐色调)
    COLOR_TX      = QColor("#3498DB")   # 发送命令 — 柔和蓝
    COLOR_RX      = QColor("#2ECC71")   # 接收应答 — 翠绿
    COLOR_EVENT   = QColor("#9B59B6")   # 接收事件 — 深紫
    COLOR_ERROR   = QColor("#E74C3C")   # 错误信息 — 珊瑚红

    def add_tx_command(self, line: str):
        """添加发送命令 (蓝色)"""
        self._append_line(line, "→", self.COLOR_TX)

    def add_rx_response(self, line: str):
        """添加接收应答 (绿色)"""
        self._append_line(line, "←", self.COLOR_RX)

    def add_rx_event(self, line: str):
        """添加接收事件 (紫色)"""
        self._append_line(line, "←", self.COLOR_EVENT)

    def add_error(self, line: str):
        """添加错误信息 (红色)"""
        self._append_line(line, "!", self.COLOR_ERROR)

    # ── 内部实现 ──

    def _append_line(self, line: str, direction: str, color: QColor):
        """追加一行带时间戳的日志

        当 "最新在上" 选中时，新条目插入到顶部；否则追加到底部。
        """
        now = datetime.now()
        timestamp = now.strftime("%H:%M:%S.") + f"{now.microsecond // 1000:03d}"

        fmt = QTextCharFormat()
        fmt.setForeground(color)

        entry = f"[{timestamp}] {direction} {line}\n"

        if self.chk_newest_first.isChecked():
            # 最新在上：光标移到开头插入
            cursor = self.log_edit.textCursor()
            cursor.movePosition(QTextCursor.Start)
            cursor.insertText(entry, fmt)
        else:
            # 最新在下：追加到底部
            cursor = self.log_edit.textCursor()
            cursor.movePosition(QTextCursor.End)
            cursor.insertText(entry, fmt)
            # 自动滚动到底部
            self.log_edit.setTextCursor(cursor)

    # ── 样式 ──

    def _apply_log_styles(self):
        """应用与调色板一致的控件样式"""
        # 复选框: 与整体 Fusion dark 主题一致
        self.chk_newest_first.setStyleSheet("""
            QCheckBox {
                color: #CCCCCC;
                spacing: 6px;
            }
            QCheckBox::indicator {
                width: 14px;
                height: 14px;
            }
        """)

        # 日志编辑区背景与主窗口背景一致
        self.log_edit.setStyleSheet("""
            QPlainTextEdit {
                background-color: #232629;
                color: #E0E0E0;
                border: 1px solid #555555;
                border-radius: 3px;
            }
        """)

    def _export_log(self):
        """导出日志到文件"""
        import os
        os.makedirs(EXPORT_DIR, exist_ok=True)

        default_name = f"uart_log_{datetime.now().strftime('%Y%m%d_%H%M%S')}.txt"
        path, _ = QFileDialog.getSaveFileName(
            self, "导出日志", f"{EXPORT_DIR}/{default_name}",
            "Text Files (*.txt);;CSV Files (*.csv);;All Files (*)",
        )
        if path:
            with open(path, "w", encoding="utf-8") as f:
                f.write(self.log_edit.toPlainText())
