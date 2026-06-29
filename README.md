# LoRa 温度实时监测

跨平台 Qt6 桌面应用，通过串口 Modbus RTU 实时采集多点温度传感器数据，提供卡片、曲线、表格三种可视化视图，自动记录到 CSV 文件，并支持上下限报警。

## 功能特性

- **实时采集**：Modbus RTU 协议，2 秒轮询，支持 1~16 个节点
- **多视图展示**：
  - 大字号温度卡片（报警时变色：超上限红色、超下限蓝色）
  - 实时温度曲线（保留最近 30 分钟数据，支持 ID 多选显示）
  - 数据表格（最近 500 条记录，最新置顶）
- **自动保存**：每次采集会话自动生成 `temp_YYYYMMDD_HHMMSS.csv`（UTF-8 BOM，Excel 友好）
- **配置持久化**：串口参数、节点配置、报警阈值自动保存，下次启动恢复
- **模拟模式**：无需硬件即可测试，生成正弦波温度数据
- **跨平台**：Linux + Windows

## 技术栈

- **语言**：C++17
- **框架**：Qt 6（Core / Gui / Widgets / SerialPort / SerialBus / Charts）
- **构建**：CMake（`qt_standard_project_setup`，自动 MOC）
- **测试**：QTest 单元测试

## 项目结构

```
LoRaTemperature/
├── CMakeLists.txt              # 根 CMake
├── src/
│   ├── CMakeLists.txt          # 应用目标
│   ├── main.cpp                # 程序入口
│   ├── Sample.h                # 采集数据结构 + 温度解析（纯函数）
│   ├── AppConfig.h/.cpp        # 配置持久化（QSettings）
│   ├── CsvWriter.h/.cpp        # CSV 追加写入
│   ├── ModbusWorker.h/.cpp     # 子线程 Modbus 轮询
│   ├── ChartManager.h/.cpp     # QtCharts 曲线管理
│   └── MainWindow.h/.cpp       # 主窗口 UI + 信号槽组装
├── tests/
│   ├── CMakeLists.txt
│   ├── tst_sample.cpp          # 温度解析单元测试
│   ├── tst_appconfig.cpp       # 配置读写测试
│   └── tst_csvwriter.cpp       # CSV 写入测试
└── data/                       # 运行时 CSV 输出目录（自动创建）
```

## 编译

### 依赖

- Qt 6.2+（含 Charts、SerialPort、SerialBus 模块）
- CMake 3.16+
- C++17 编译器

### Linux 编译

```bash
# 指定 Qt6 安装路径（如非系统默认位置）
export CMAKE_PREFIX_PATH=/home/hex/Qt/6.10.1/gcc_64

cmake -B build -S .
cmake --build build -j$(nproc)
```

### Windows 编译

```cmd
set CMAKE_PREFIX_PATH=C:\Qt\6.10.1\msvc2019_64
cmake -B build -S .
cmake --build build --config Release
```

### 运行测试

```bash
ctest --test-dir build --output-on-failure
```

预期输出：3 个测试套件（tst_sample / tst_appconfig / tst_csvwriter）全部 PASS。

## 运行

```bash
./build/src/LoRaTemperature
```

## 使用说明

1. **选择串口**：在左侧配置面板选择 USB 转 RS485 对应的串口
2. **配置参数**：波特率 9600、从机地址 1、起始 ID 1、节点数 2、采样周期 2000ms
3. **点击"开始"**：应用自动创建 CSV 文件并开始采集
4. **观察数据**：
   - 右上卡片显示各节点当前温度
   - 中部曲线显示温度变化趋势
   - 底部表格显示历史记录
5. **点击"停止"**：关闭 CSV 文件，可用 Excel 打开查看

## Modbus 协议参数

| 参数 | 值 |
|------|-----|
| 功能码 | 0x04（读输入寄存器） |
| 寄存器起始地址 | 0x76C1 |
| 寄存器数量 | 节点数 |
| 从机地址 | 1 |
| 串口格式 | 9600 bps / 8 数据位 / 1 停止位 / 无校验 |

### 温度数据解析

寄存器值为 16 位有符号整数（补码，大端），除以 10 得到摄氏温度：

- `0x00BF` (191) → 19.1 ℃
- `0xFF60` (65376 → -160) → -16.0 ℃
- `0x0000` (0) → 0.0 ℃

## CSV 文件格式

文件名：`temp_YYYYMMDD_HHMMSS.csv`
编码：UTF-8 with BOM

| 列 | 说明 | 示例 |
|----|------|------|
| timestamp | 采集时间 | 2026-06-29 14:30:01.123 |
| node_id | 节点 ID | 1 |
| temp_celsius | 温度（℃） | 19.1 |
| raw | 寄存器原始值 | 191 |
| online | 在线状态 | 1 |
| alarm | 报警状态 | 0=正常 / 1=超上限 / -1=超下限 |

## 配置持久化

配置存储位置：
- **Linux**：`~/.config/LoRaTemperature/LoRaTemperature.conf`
- **Windows**：注册表 `HKEY_CURRENT_USER\Software\LoRaTemperature\LoRaTemperature`

可配置项：串口名、波特率、数据位、停止位、校验位、从机地址、起始节点 ID、节点数、采样周期、寄存器地址、CSV 目录、各节点报警上下限。

## 报警功能

每个节点可独立配置上下限阈值（默认下限 -10 ℃，上限 60 ℃）：
- 超过上限：卡片变红
- 低于下限：卡片变蓝
- 正常：卡片灰色

阈值在配置文件中持久化，下次启动自动恢复。

## 架构说明

采用信号槽解耦的模块化设计：

```
ModbusWorker (子线程)
    │ dataReady(QVector<Sample>)
    ▼
MainWindow (主线程)
    ├── CsvWriter      → 写 CSV 文件
    ├── ChartManager   → 更新曲线
    ├── 卡片标签        → 更新当前值
    └── 表格           → 插入历史记录
```

- `ModbusWorker` 运行在子线程，通过 `QThread + moveToThread` 实现，避免阻塞 UI
- 跨线程信号槽自动使用 `Qt::QueuedConnection`，已注册 `QVector<Sample>` 和 `AppConfig` 的 metatype
- 各模块职责单一，便于独立测试和维护

## 许可证

本项目为私有项目，保留所有权利。
