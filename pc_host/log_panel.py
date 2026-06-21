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
    QPlainTextEdit, QPushButton, QLabel, QFileDialog,
)
from PyQt5.QtCore import Qt
from PyQt5.QtGui import QTextCursor, QColor, QTextCharFormat


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
        header.addWidget(QLabel("收发日志 (最新在上)"))
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
        self.log_edit.setFont(self.log_edit.font())  # 使用等宽字体更好
        layout.addWidget(self.log_edit)

        # 连接信号
        self.btn_clear.clicked.connect(self.log_edit.clear)
        self.btn_export.clicked.connect(self._export_log)

    # ── 公开方法 ──

    def add_tx_command(self, line: str):
        """添加发送命令 (蓝色)"""
        self._append_line(line, "→", QColor("#0066CC"))

    def add_rx_response(self, line: str):
        """添加接收应答 (绿色)"""
        self._append_line(line, "←", QColor("#008800"))

    def add_rx_event(self, line: str):
        """添加接收事件 (紫色)"""
        self._append_line(line, "←", QColor("#8800AA"))

    def add_error(self, line: str):
        """添加错误信息 (红色)"""
        self._append_line(line, "!", QColor("#CC0000"))

    # ── 内部实现 ──

    def _append_line(self, line: str, direction: str, color: QColor):
        """追加一行带时间戳的日志"""
        now = datetime.now()
        timestamp = now.strftime("%H:%M:%S.") + f"{now.microsecond // 1000:03d}"

        fmt = QTextCharFormat()
        fmt.setForeground(color)

        cursor = self.log_edit.textCursor()
        cursor.movePosition(QTextCursor.End)
        cursor.insertText(f"[{timestamp}] {direction} {line}\n", fmt)

        # 自动滚动到底部
        self.log_edit.setTextCursor(cursor)

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
