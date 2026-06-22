# S800 智能互联时钟 — PC 上位机

## 启动

```bash
# 1. 安装依赖
pip install -r requirements.txt

# 2. 运行
python main.py
```

## 功能概览

### 核心功能 (必做 P1-P5)
- **P1 串口管理**: 自动扫描 COM, 连接/断开, 1Hz 心跳, 延迟显示
- **P2 控制面板**: 全部 12+ 条命令可视化下发, 参数组合 ≥3 种, 缩写/大小写演示
- **P3 数字孪生**: 8 位 7SEG + 8 位 LED + 8 按键 + USER1/USER2, 双向同步, NIGHT 模式正确渲染
- **P4 收发日志**: 时间戳 + 方向标记 + 颜色编码, 可导出
- **P5 异常处理**: 串口占用/解析异常/超时 → 弹窗或日志高亮, GUI 不崩溃

### 扩展功能 (选做 E1-E4)
- **E1 NTP 对时**: 后台获取 aliyun/pool NTP 标准时间，按钮与 USER1 共用流程，成功后板端 LED4 常亮
- **E2 天气获取**: Open-Meteo 主源 + wttr.in 备用，代理失败自动直连，后台刷新，USER2 短显
- **E3 自动昼夜**: astral 计算日出日落, 自动 *SET:MODE DAY/NIGHT
- **E4 可视化看板**: events.csv 持久化, 3 种 matplotlib 图表

## 项目结构

```
pc_host/
├── main.py                # 程序入口, QMainWindow
├── serial_worker.py       # 后台串口线程 (QThread)
├── protocol.py            # 协议解析器 (纯 Python, 可测试)
├── twin_panel.py          # 数字孪生镜像面板
│                           #   - SevenSegWidget (QPainter 7 段)
│                           #   - LedIndicator (QSS 径向渐变)
│                           #   - 虚拟按键 K1-K8 + USER1/USER2
├── control_panel.py       # 控制面板 (全部命令 GUI)
├── log_panel.py           # 收发日志面板
├── ntp_helper.py          # NTP 对时助手
├── weather_helper.py      # 天气获取助手
├── tests/
│   └── test_weather_helper.py # 天气源/代理/缓存/LED 单元测试
├── auto_daynight.py       # 自动昼夜模式
├── dashboard.py           # 数据记录与可视化
├── ui/
│   └── main_window.ui     # Qt Designer 布局文件
├── ui_main_window.py      # pyuic5 自动生成
├── logs/                  # 日志输出目录
│   └── events.csv         # 事件持久化
├── requirements.txt       # Python 依赖
├── CLAUDE.md              # PC Host 开发指南
└── README.md              # 本文件
```

## 技术栈

| 组件 | 版本 | 用途 |
|------|------|------|
| Python | 3.11.9 | 运行时 |
| PyQt5 | 5.15.9 | GUI |
| pyserial | ≥3.5 | 串口通信 |
| ntplib | ≥0.4.0 | NTP 对时 |
| requests | ≥2.28 | HTTP (天气) |
| astral | ≥3.2 | 日出日落计算 |
| matplotlib | ≥3.5 | 图表绘制 |

## 天气容错

- 默认查询上海 Open-Meteo，失败后自动切换 wttr.in。
- 优先遵循系统 `HTTP_PROXY` / `HTTPS_PROXY`；发生代理或 TLS 握手错误时保持证书校验并尝试直连。
- 两个实时源均不可用时，最近两小时内的成功结果可作为缓存继续用于 USER2 短显。
- 所有请求在后台线程执行，不阻塞 Qt GUI。

```powershell
python -m unittest discover -s pc_host\tests -p "test_*.py" -v
```
