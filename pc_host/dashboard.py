"""
数据可视化看板 (E4)
====================
- 持久化事件到 logs/events.csv
- 闹钟触发分布柱状图
- NTP 同步偏差散点图
- 按键热度图
- 至少 3 种图表
"""

import os
import csv
from datetime import datetime
from collections import defaultdict

CSV_PATH = "logs/events.csv"
CSV_HEADER = ["timestamp", "type", "field", "value"]


class Dashboard:
    """
    数据记录与分析看板
    NOTE: matplotlib 嵌入 PyQt 的 FigureCanvasQTAgg 需要 QApplication 已创建。
    在使用图表前调用 ensure_csv_exists() 和 log_event() 记录数据。
    """

    def __init__(self):
        self._ensure_csv_exists()

    # ── 数据记录 ──

    @staticmethod
    def _ensure_csv_exists():
        """初始化 CSV 文件（如果不存在）"""
        os.makedirs(os.path.dirname(CSV_PATH), exist_ok=True)
        if not os.path.exists(CSV_PATH):
            with open(CSV_PATH, "w", newline="", encoding="utf-8") as f:
                writer = csv.writer(f)
                writer.writerow(CSV_HEADER)

    @staticmethod
    def log_event(event_type: str, field: str = "", value: str = ""):
        """记录一条事件到 CSV"""
        timestamp = datetime.now().isoformat(timespec="seconds")
        try:
            with open(CSV_PATH, "a", newline="", encoding="utf-8") as f:
                writer = csv.writer(f)
                writer.writerow([timestamp, event_type, field, value])
        except Exception:
            pass  # 日志失败不影响主程序

    # ── 数据分析 (供图表使用) ──

    def load_events(self) -> list[dict]:
        """加载所有事件记录"""
        events = []
        if not os.path.exists(CSV_PATH):
            return events
        try:
            with open(CSV_PATH, "r", encoding="utf-8") as f:
                reader = csv.DictReader(f)
                for row in reader:
                    events.append(row)
        except Exception:
            pass
        return events

    def get_alarm_distribution(self) -> dict[int, int]:
        """闹钟触发按小时分布"""
        dist: dict[int, int] = defaultdict(int)
        for e in self.load_events():
            if e.get("type") == "ALARM":
                try:
                    ts = datetime.fromisoformat(e["timestamp"])
                    dist[ts.hour] += 1
                except (ValueError, KeyError):
                    pass
        return dict(sorted(dist.items()))

    def get_ntp_deltas(self) -> list[tuple[datetime, float]]:
        """NTP 同步偏差时间序列"""
        deltas = []
        for e in self.load_events():
            if e.get("type") == "SYNC" and e.get("value"):
                try:
                    ts = datetime.fromisoformat(e["timestamp"])
                    delta = float(e["value"])
                    deltas.append((ts, delta))
                except (ValueError, KeyError):
                    pass
        return deltas

    def get_key_heatmap(self) -> dict[str, int]:
        """按键按下频率统计"""
        counts: dict[str, int] = defaultdict(int)
        for e in self.load_events():
            if e.get("type") == "KEY":
                name = e.get("field", "")
                if name:
                    counts[name] += 1
        return dict(counts)
