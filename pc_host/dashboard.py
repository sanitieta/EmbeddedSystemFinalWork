"""
数据可视化看板 (E4)
====================
- 持久化事件到 logs/events.csv
- 闹钟触发分布柱状图
- NTP 同步偏差散点图
- 按键热度图
- QTabWidget 嵌入 matplotlib 图表窗口
"""

import os
import csv
from datetime import datetime
from collections import defaultdict

# ── matplotlib 导入（缺失时导入 None，不阻断模块加载）──
try:
    import matplotlib
    matplotlib.use("Qt5Agg")
    import matplotlib.pyplot as plt
    from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg as FigureCanvas
    _MPL_AVAILABLE = True
except ImportError:
    _MPL_AVAILABLE = False
    FigureCanvas = None
    plt = None

CSV_PATH = "logs/events.csv"
CSV_HEADER = ["timestamp", "type", "field", "value"]


# ═══════════════════════════════════════════════════════════════════════════════
#  Dashboard — 数据记录与分析
# ═══════════════════════════════════════════════════════════════════════════════

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


# ═══════════════════════════════════════════════════════════════════════════════
#  DashboardWindow — 独立可视化窗口
# ═══════════════════════════════════════════════════════════════════════════════

if _MPL_AVAILABLE:
    from PyQt5.QtWidgets import (QWidget, QVBoxLayout, QTabWidget, QLabel,
                                 QApplication)
    from PyQt5.QtCore import QTimer

    # 全局 matplotlib 配置
    plt.style.use("dark_background")

    # 覆盖 facecolor 以匹配 Qt Fusion dark 主题调色板
    MATPLOTLIB_BG        = "#31363B"   # figure facecolor — 匹配主窗口背景
    MATPLOTLIB_AXES_BG   = "#232629"   # axes facecolor — 匹配输入控件背景 (略深)
    MATPLOTLIB_TEXT      = "#EFF0F1"   # 文本色 — 匹配 Fusion dark 前景
    MATPLOTLIB_GRID      = "#555555"   # 网格线

    matplotlib.rcParams.update({
        "figure.facecolor":            MATPLOTLIB_BG,
        "axes.facecolor":              MATPLOTLIB_AXES_BG,
        "axes.edgecolor":              MATPLOTLIB_GRID,
        "axes.labelcolor":             MATPLOTLIB_TEXT,
        "text.color":                  MATPLOTLIB_TEXT,
        "xtick.color":                 MATPLOTLIB_TEXT,
        "ytick.color":                 MATPLOTLIB_TEXT,
        "grid.color":                  MATPLOTLIB_GRID,
        "grid.alpha":                  0.3,
        "legend.facecolor":            MATPLOTLIB_AXES_BG,
        "legend.edgecolor":            MATPLOTLIB_GRID,
        "font.sans-serif":             ["Microsoft YaHei", "SimHei"],
        "axes.unicode_minus":          False,
    })

    # 按键顺序（横向柱状图从上到下的显示顺序）
    _KEY_ORDER = ["K1", "K2", "K3", "K4", "K5", "K6", "K7", "K8",
                  "USER1", "USER2", "EXT"]

    # 按键柱状图的颜色（与 11 个按键一一对应）
    _KEY_COLORS = [
        "#FF6B6B", "#FFA94D", "#FFD43B", "#69DB7C",
        "#4DABF7", "#748FFC", "#9775FA", "#F783AC",
        "#63E6BE", "#FCC419", "#CED4DA",
    ]


    class _EmptyDataAxes:
        """
        在 Axes 中央显示"暂无数据"的支持对象。

        使用方法:
            ax = figure.add_subplot(111)
            empty = _EmptyDataAxes(ax)
            if not data:
                empty.show()
            else:
                empty.hide()
                # 正常绘图 ...
        """

        def __init__(self, axes):
            self._axes = axes
            self._text: object = None  # matplotlib.text.Text

        def show(self) -> None:
            """显示"暂无数据"提示"""
            if self._text is None:
                self._text = self._axes.text(
                    0.5, 0.5, "暂无数据",
                    transform=self._axes.transAxes,
                    fontsize=20, color="#AAAAAA",
                    ha="center", va="center"
                )
            self._text.set_visible(True)
            self._axes.set_xticks([])
            self._axes.set_yticks([])

        def hide(self) -> None:
            """隐藏提示 (准备正常绘图)"""
            if self._text is not None:
                self._text.set_visible(False)
            # 恢复默认 tick 行为
            self._axes.tick_params(which="both", reset=True)

        @property
        def axes(self):
            return self._axes


    class DashboardWindow(QWidget):
        """
        独立的数据可视化窗口。

        包含三个标签页:
          - "闹钟分布": 闹钟触发按小时分布的柱状图
          - "NTP 偏差": NTP 同步偏差的散点连线图
          - "按键热度": 按键按下频率的横向柱状图

        使用方式:
            dash = Dashboard()       # 数据后端
            win = DashboardWindow(dash)
            win.show()
            win.start_refresh()     # 开始每 5 秒自动刷新
        """

        def __init__(self, dashboard: Dashboard, parent: QWidget = None):
            super().__init__(parent)
            self._dashboard = dashboard

            self._setup_ui()
            self._setup_charts()

            # 自动刷新定时器 (5 秒)
            self._refresh_timer = QTimer(self)
            self._refresh_timer.timeout.connect(self._refresh_all)
            self._refresh_timer.start(5000)

        # ── UI 布局 ──

        def _setup_ui(self) -> None:
            """创建主布局和 QTabWidget"""
            self.setWindowTitle("数据可视化 (E4)")
            self.resize(800, 600)

            layout = QVBoxLayout(self)
            layout.setContentsMargins(0, 0, 0, 0)

            self._tabs = QTabWidget(self)
            layout.addWidget(self._tabs)

        def _setup_charts(self) -> None:
            """为每个标签页创建 matplotlib Figure + Canvas"""
            # ── Tab 1: 闹钟分布 ──
            self._alarm_fig, self._alarm_ax = plt.subplots(figsize=(8, 5))
            self._alarm_ax.set_title("闹钟触发分布 (按小时)", fontsize=14,
                                     fontweight="bold", color="#CCCCCC")
            self._alarm_ax.set_xlabel("小时", fontsize=11)
            self._alarm_ax.set_ylabel("触发次数", fontsize=11)
            self._alarm_empty = _EmptyDataAxes(self._alarm_ax)

            self._alarm_canvas = FigureCanvas(self._alarm_fig)
            self._alarm_canvas.setParent(self)
            self._tabs.addTab(self._alarm_canvas, "闹钟分布")

            # ── Tab 2: NTP 偏差 ──
            self._ntp_fig, self._ntp_ax = plt.subplots(figsize=(8, 5))
            self._ntp_ax.set_title("NTP 同步偏差", fontsize=14,
                                    fontweight="bold", color="#CCCCCC")
            self._ntp_ax.set_xlabel("时间", fontsize=11)
            self._ntp_ax.set_ylabel("偏差 (ms)", fontsize=11)
            self._ntp_empty = _EmptyDataAxes(self._ntp_ax)

            self._ntp_canvas = FigureCanvas(self._ntp_fig)
            self._ntp_canvas.setParent(self)
            self._tabs.addTab(self._ntp_canvas, "NTP 偏差")

            # ── Tab 3: 按键热度 ──
            self._key_fig, self._key_ax = plt.subplots(figsize=(8, 5))
            self._key_ax.set_title("按键热度", fontsize=14,
                                    fontweight="bold", color="#CCCCCC")
            self._key_ax.set_xlabel("按下次数", fontsize=11)
            self._key_ax.set_ylabel("按键", fontsize=11)
            self._key_empty = _EmptyDataAxes(self._key_ax)

            self._key_canvas = FigureCanvas(self._key_fig)
            self._key_canvas.setParent(self)
            self._tabs.addTab(self._key_canvas, "按键热度")

            # 初始绘制
            self._refresh_all()

        # ── 数据刷新 ──

        def _refresh_all(self) -> None:
            """刷新全部三个图表"""
            self._draw_alarm_tab()
            self._draw_ntp_tab()
            self._draw_key_tab()

        # ── Tab 1: 闹钟分布 ──

        def _draw_alarm_tab(self) -> None:
            """绘制闹钟触发按小时分布的柱状图"""
            self._alarm_ax.clear()
            self._alarm_empty._text = None  # 重置文本引用

            dist = self._dashboard.get_alarm_distribution()

            if not dist:
                self._alarm_empty.show()
                self._alarm_canvas.draw()
                return

            self._alarm_empty.hide()

            # 确保 X 轴覆盖 0–23 小时
            hours = list(range(24))
            counts = [dist.get(h, 0) for h in hours]

            bars = self._alarm_ax.bar(hours, counts, color="#FF6B6B",
                                      edgecolor="#DD4444", linewidth=0.5)
            self._alarm_ax.set_title("闹钟触发分布 (按小时)", fontsize=14,
                                     fontweight="bold", color="#CCCCCC")
            self._alarm_ax.set_xlabel("小时", fontsize=11)
            self._alarm_ax.set_ylabel("触发次数", fontsize=11)
            self._alarm_ax.set_xticks(hours)
            self._alarm_ax.set_xticklabels([str(h) for h in hours], fontsize=9)
            self._alarm_ax.set_xlim(-0.8, 23.8)

            # 在柱顶部标注数值
            max_y = max(counts) if counts else 1
            for bar_obj, val in zip(bars, counts):
                if val > 0:
                    self._alarm_ax.text(
                        bar_obj.get_x() + bar_obj.get_width() / 2,
                        bar_obj.get_height() + max_y * 0.02,
                        str(val), ha="center", va="bottom",
                        fontsize=8, color="#FFFFFF"
                    )

            self._alarm_ax.set_ylim(0, max_y * 1.15 if max_y > 0 else 1)
            self._alarm_fig.tight_layout()
            self._alarm_canvas.draw()

        # ── Tab 2: NTP 偏差 ──

        def _draw_ntp_tab(self) -> None:
            """绘制 NTP 同步偏差散点 + 连线图"""
            self._ntp_ax.clear()
            self._ntp_empty._text = None

            deltas = self._dashboard.get_ntp_deltas()

            if not deltas:
                self._ntp_empty.show()
                self._ntp_canvas.draw()
                return

            self._ntp_empty.hide()

            timestamps, offsets = zip(*deltas)

            # 散点 + 连线
            self._ntp_ax.plot(timestamps, offsets, color="#4DABF7",
                              marker="o", markersize=6, linewidth=1.5,
                              markerfacecolor="#74C0FC",
                              markeredgecolor="#1E90FF",
                              markeredgewidth=0.5)

            # 零偏差参考线 (绿色虚线)
            self._ntp_ax.axhline(y=0, color="#69DB7C", linestyle="--",
                                 linewidth=1, alpha=0.7, label="零偏差")

            self._ntp_ax.set_title("NTP 同步偏差", fontsize=14,
                                    fontweight="bold", color="#CCCCCC")
            self._ntp_ax.set_xlabel("时间", fontsize=11)
            self._ntp_ax.set_ylabel("偏差 (ms)", fontsize=11)
            self._ntp_ax.legend(loc="upper right", fontsize=9,
                                framealpha=0.6)
            self._ntp_ax.tick_params(axis="x", rotation=30, labelsize=8)

            self._ntp_fig.tight_layout()
            self._ntp_canvas.draw()

        # ── Tab 3: 按键热度 ──

        def _draw_key_tab(self) -> None:
            """绘制按键按下频率横向柱状图"""
            self._key_ax.clear()
            self._key_empty._text = None

            counts = self._dashboard.get_key_heatmap()

            # 只保留 _KEY_ORDER 中有数据的按键 (至少出现过)
            present_keys = [k for k in _KEY_ORDER if k in counts]
            if not present_keys:
                self._key_empty.show()
                self._key_canvas.draw()
                return

            self._key_empty.hide()

            y_labels = present_keys[::-1]  # 倒序, 从上到下
            y_values = [counts[k] for k in y_labels]
            y_indices = range(len(y_labels))

            # 颜色映射: 使用 _KEY_ORDER 中的位置匹配 _KEY_COLORS
            bar_colors = []
            for k in y_labels:
                try:
                    idx = _KEY_ORDER.index(k)
                    bar_colors.append(_KEY_COLORS[idx])
                except ValueError:
                    bar_colors.append("#CED4DA")

            bars = self._key_ax.barh(y_indices, y_values, height=0.6,
                                     color=bar_colors, edgecolor="#444444",
                                     linewidth=0.5)

            self._key_ax.set_yticks(list(y_indices))
            self._key_ax.set_yticklabels(y_labels, fontsize=10)
            self._key_ax.set_title("按键热度", fontsize=14,
                                    fontweight="bold", color="#CCCCCC")
            self._key_ax.set_xlabel("按下次数", fontsize=11)
            self._key_ax.set_ylabel("按键", fontsize=11)

            # 在每个柱的末端标注数值
            max_x = max(y_values) if y_values else 1
            for bar_obj, val in zip(bars, y_values):
                if val > 0:
                    self._key_ax.text(
                        bar_obj.get_width() + max_x * 0.02,
                        bar_obj.get_y() + bar_obj.get_height() / 2,
                        str(val), va="center", fontsize=9,
                        color="#FFFFFF"
                    )

            self._key_ax.set_xlim(0, max_x * 1.2 if max_x > 0 else 1)
            self._key_fig.tight_layout()
            self._key_canvas.draw()

        # ── 公开接口 ──

        def start_refresh(self) -> None:
            """开始 (或恢复) 定时刷新"""
            if not self._refresh_timer.isActive():
                self._refresh_timer.start(5000)

        def stop_refresh(self) -> None:
            """停止定时刷新"""
            self._refresh_timer.stop()

        def refresh_now(self) -> None:
            """立即刷新 (不等待定时器)"""
            self._refresh_all()

else:
    # matplotlib 不可用 — 提供占位 Widget
    from PyQt5.QtWidgets import QWidget, QVBoxLayout, QLabel

    class DashboardWindow(QWidget):
        """matplotlib 不可用时的占位窗口"""

        def __init__(self, dashboard: Dashboard, parent: QWidget = None):
            super().__init__(parent)
            self.setWindowTitle("数据可视化 (E4)")
            self.resize(600, 300)

            layout = QVBoxLayout(self)
            label = QLabel(
                "matplotlib 未安装或导入失败。\n"
                "请运行: pip install matplotlib\n\n"
                "此功能需要 matplotlib >= 3.5",
                self
            )
            label.setAlignment(0x0004 | 0x0080)  # Qt.AlignHCenter | Qt.AlignVCenter
            label.setStyleSheet("color: #AAAAAA; font-size: 14px;")
            layout.addWidget(label)

        def start_refresh(self) -> None:
            pass

        def stop_refresh(self) -> None:
            pass

        def refresh_now(self) -> None:
            pass
