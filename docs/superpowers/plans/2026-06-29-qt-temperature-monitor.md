# LoRa 温度实时监测 Qt 应用 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现一个跨平台 Qt6 C++ 桌面应用，通过串口 Modbus RTU 每 2 秒读取 ID1、ID2 的温度1寄存器（0x76C1），实时显示并自动保存 CSV。

**Architecture:** 子线程 `ModbusWorker` 定时轮询 Modbus，发 `dataReady(QVector<Sample>)` 信号；主线程的卡片/曲线/表格/CSV 各自独立响应。信号槽解耦，模块边界清晰。

**Tech Stack:** Qt6 (Core/Gui/Widgets/SerialPort/SerialBus/Charts) + C++17 + CMake + QTest

## Global Constraints

- 语言：C++17
- 框架：Qt6，模块 Core/Gui/Widgets/SerialPort/SerialBus/Charts/Test
- 构建：CMake（用 `qt_add_executable` / `qt_add_library`，自动 MOC）
- 协议：功能码 0x04，温度1寄存器起始 `0x76C1`，2 秒采样，ID1+ID2
- 数据解析：`quint16` → `qint16`（补码）→ ÷10.0 = ℃
- 串口格式：8 数据位 / 1 停止位 / 无校验 / 9600 bps / 从机地址 1
- CSV：UTF-8 with BOM，文件名 `temp_YYYYMMDD_HHMMSS.csv`
- 跨平台：Linux + Windows
- 配置持久化：QSettings
- 注释用中文

## 文件结构

```
LoRaTemperature/
├── CMakeLists.txt              # 根 CMake，find_package Qt6
├── src/
│   ├── CMakeLists.txt          # 应用目标
│   ├── main.cpp                # 程序入口
│   ├── Sample.h                # 采集数据结构 + 温度解析（纯函数，可测）
│   ├── AppConfig.h/.cpp        # 配置持久化（QSettings 封装）
│   ├── CsvWriter.h/.cpp        # CSV 追加写入
│   ├── ModbusWorker.h/.cpp     # 子线程 Modbus 轮询
│   ├── ChartManager.h/.cpp     # QtCharts 曲线管理
│   └── MainWindow.h/.cpp       # 主窗口 UI + 信号槽组装
├── tests/
│   ├── CMakeLists.txt
│   ├── tst_sample.cpp          # Sample 解析单元测试
│   ├── tst_appconfig.cpp       # 配置读写测试
│   └── tst_csvwriter.cpp       # CSV 写入测试
└── data/                       # 运行时 CSV 输出目录（自动创建）
```

**模块边界：**
- `Sample.h`：纯数据 + 纯函数，无 Qt 依赖以外的依赖，可独立测试
- `AppConfig`：读写 QSettings，不依赖其他业务模块
- `CsvWriter`：收 `Sample` 写文件，不依赖数据来源
- `ModbusWorker`：只发 `dataReady` 信号，不碰 UI/文件
- `ChartManager`：只更新图表，不碰存储
- `MainWindow`：组装 UI + 信号槽连线

---

## Task 1: 项目骨架 + Sample 数据结构与温度解析

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/CMakeLists.txt`
- Create: `src/Sample.h`
- Create: `tests/CMakeLists.txt`
- Create: `tests/tst_sample.cpp`

**Interfaces:**
- Produces: `struct Sample { qint64 timestampMs; int nodeId; quint16 raw; qreal tempCelsius; int online; int alarm; }`；`qreal parseTempCelsius(quint16 raw)`；`int checkAlarm(qreal temp, qreal low, qreal high)`

- [ ] **Step 1: 写根 CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.16)
project(LoRaTemperature VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(QT_COMPONENTS Core Gui Widgets SerialPort SerialBus Charts Test)
find_package(Qt6 REQUIRED COMPONENTS ${QT_COMPONENTS})

qt_standard_project_setup()

add_subdirectory(src)
enable_testing()
add_subdirectory(tests)
```

- [ ] **Step 2: 写 src/CMakeLists.txt**

```cmake
qt_add_executable(LoRaTemperature
    main.cpp
    Sample.h
    AppConfig.h
    AppConfig.cpp
    CsvWriter.h
    CsvWriter.cpp
    ModbusWorker.h
    ModbusWorker.cpp
    ChartManager.h
    ChartManager.cpp
    MainWindow.h
    MainWindow.cpp
)

target_link_libraries(LoRaTemperature PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::SerialPort
    Qt6::SerialBus
    Qt6::Charts
)

target_include_directories(LoRaTemperature PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
```

> 说明：此时 main.cpp / 其他模块尚未创建，先创建空壳文件让 CMake 配置通过。Step 3 之后逐步填充。本任务先只建 `Sample.h`，其余文件创建空壳（空 .cpp / 仅 `#pragma once` 的 .h），main.cpp 给最小空壳。

- [ ] **Step 3: 创建 Sample.h（含解析函数）**

```cpp
#pragma once
#include <QtGlobal>

// 单次采集数据样本
struct Sample {
    qint64  timestampMs;   // 采集时间戳（epoch 毫秒）
    int     nodeId;        // 节点 ID（1~16）
    quint16 raw;           // 寄存器原始值
    qreal   tempCelsius;   // 解析后温度（℃）
    int     online;        // 1=本次成功读取，0=未读到
    int     alarm;         // 0=正常，1=超上限，-1=超下限
};

// 温度解析：Modbus 大端 quint16 -> qint16 补码 -> /10.0
// 例：raw=0x00BF(191) -> 19.1℃；raw=0xFF60(65376) -> -16.0℃
inline qreal parseTempCelsius(quint16 raw) {
    return static_cast<qint16>(raw) / 10.0;
}

// 报警判定：返回 0 正常 / 1 超上限 / -1 超下限
inline int checkAlarm(qreal temp, qreal low, qreal high) {
    if (temp > high)  return 1;
    if (temp < low)   return -1;
    return 0;
}
```

- [ ] **Step 4: 创建其余空壳文件，保证 CMake 配置通过**

`src/main.cpp`：
```cpp
#include <QApplication>
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    return app.exec();
}
```

`src/AppConfig.h`：`#pragma once`
`src/AppConfig.cpp`：空文件
`src/CsvWriter.h`：`#pragma once`
`src/CsvWriter.cpp`：空文件
`src/ModbusWorker.h`：`#pragma once`
`src/ModbusWorker.cpp`：空文件
`src/ChartManager.h`：`#pragma once`
`src/ChartManager.cpp`：空文件
`src/MainWindow.h`：`#pragma once`
`src/MainWindow.cpp`：空文件

- [ ] **Step 5: 写 tests/CMakeLists.txt**

```cmake
qt_add_test(tst_sample SOURCES tst_sample.cpp)
target_link_libraries(tst_sample PRIVATE Qt6::Core)
target_include_directories(tst_sample PRIVATE ${CMAKE_SOURCE_DIR}/src)
```

> 后续任务会在此文件追加 tst_appconfig / tst_csvwriter。

- [ ] **Step 6: 写 tst_sample.cpp（失败测试）**

```cpp
#include <QtTest/QtTest>
#include "Sample.h"

class TestSample : public QObject {
    Q_OBJECT
private slots:
    void parsePositive();      // 191 -> 19.1
    void parseNegative();      // 0xFF60 -> -16.0
    void parseZero();          // 0 -> 0.0
    void alarmNormal();
    void alarmHigh();
    void alarmLow();
};

void TestSample::parsePositive() {
    QCOMPARE(parseTempCelsius(0x00BF), 19.1);
}
void TestSample::parseNegative() {
    QCOMPARE(parseTempCelsius(0xFF60), -16.0);
}
void TestSample::parseZero() {
    QCOMPARE(parseTempCelsius(0x0000), 0.0);
}
void TestSample::alarmNormal() {
    QCOMPARE(checkAlarm(25.0, -10.0, 60.0), 0);
}
void TestSample::alarmHigh() {
    QCOMPARE(checkAlarm(60.1, -10.0, 60.0), 1);
}
void TestSample::alarmLow() {
    QCOMPARE(checkAlarm(-10.1, -10.0, 60.0), -1);
}

QTEST_APPLESS_MAIN(TestSample)
#include "tst_sample.moc"
```

- [ ] **Step 7: 配置并编译，运行测试**

```bash
cmake -B build -S .
cmake --build build
ctest --test-dir build -R tst_sample --output-on-failure
```
Expected: 6 个测试全部 PASS。

- [ ] **Step 8: 提交**

```bash
git add CMakeLists.txt src tests
git commit -m "feat: 项目骨架与 Sample 温度解析"
```

---

## Task 2: AppConfig 配置持久化

**Files:**
- Modify: `src/AppConfig.h`
- Modify: `src/AppConfig.cpp`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/tst_appconfig.cpp`

**Interfaces:**
- Produces: `class AppConfig`，字段：`portName, baudRate, dataBits, stopBits, parity, slaveAddr, startNodeId, nodeCount, samplePeriodMs, tempRegAddr, csvDir, alarmLow(nodeId), alarmHigh(nodeId)`；方法 `load()/save()`；用 `QSettings` 存储在 `~/.config/LoRaTemperature/LoRaTemperature.ini`（Linux）或注册表（Windows）。

- [ ] **Step 1: 写 src/AppConfig.h**

```cpp
#pragma once
#include <QString>
#include <QHash>

class AppConfig {
public:
    // 串口
    QString portName;
    int     baudRate    = 9600;
    int     dataBits    = 8;
    int     stopBits    = 1;     // 1 或 2
    int     parity      = 0;     // 0=无 1=奇 2=偶
    int     slaveAddr   = 1;

    // 节点
    int     startNodeId      = 1;
    int     nodeCount        = 2;
    int     samplePeriodMs   = 2000;
    quint16 tempRegAddr      = 0x76C1;

    // CSV
    QString csvDir;             // 空=默认 data/

    // 报警阈值（key=nodeId）
    QHash<int, qreal> alarmLow;
    QHash<int, qreal> alarmHigh;

    void load();                // 从 QSettings 读
    void save();                // 写 QSettings
    void ensureDefaults();      // 填充默认阈值/CSV目录
    QString resolvedCsvDir() const; // 返回绝对路径，空则用 <appdir>/data
};
```

- [ ] **Step 2: 写 src/AppConfig.cpp**

```cpp
#include "AppConfig.h"
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QCoreApplication>

void AppConfig::ensureDefaults() {
    if (csvDir.isEmpty()) {
        csvDir = QCoreApplication::applicationDirPath() + "/data";
    }
    for (int id = startNodeId; id < startNodeId + nodeCount; ++id) {
        if (!alarmLow.contains(id))  alarmLow[id]  = -10.0;
        if (!alarmHigh.contains(id)) alarmHigh[id] = 60.0;
    }
}

QString AppConfig::resolvedCsvDir() const {
    return csvDir;
}

void AppConfig::load() {
    QSettings s;
    portName        = s.value("serial/portName", portName).toString();
    baudRate        = s.value("serial/baudRate", baudRate).toInt();
    dataBits        = s.value("serial/dataBits", dataBits).toInt();
    stopBits        = s.value("serial/stopBits", stopBits).toInt();
    parity          = s.value("serial/parity", parity).toInt();
    slaveAddr       = s.value("serial/slaveAddr", slaveAddr).toInt();
    startNodeId     = s.value("node/startNodeId", startNodeId).toInt();
    nodeCount       = s.value("node/nodeCount", nodeCount).toInt();
    samplePeriodMs  = s.value("node/samplePeriodMs", samplePeriodMs).toInt();
    tempRegAddr     = static_cast<quint16>(s.value("node/tempRegAddr", tempRegAddr).toUInt());
    csvDir          = s.value("csv/dir", csvDir).toString();

    s.beginGroup("alarm");
    for (int id = startNodeId; id < startNodeId + nodeCount; ++id) {
        alarmLow[id]  = s.value(QString::number(id) + "/low",  -10.0).toDouble();
        alarmHigh[id] = s.value(QString::number(id) + "/high",  60.0).toDouble();
    }
    s.endGroup();
    ensureDefaults();
}

void AppConfig::save() {
    QSettings s;
    s.setValue("serial/portName", portName);
    s.setValue("serial/baudRate", baudRate);
    s.setValue("serial/dataBits", dataBits);
    s.setValue("serial/stopBits", stopBits);
    s.setValue("serial/parity", parity);
    s.setValue("serial/slaveAddr", slaveAddr);
    s.setValue("node/startNodeId", startNodeId);
    s.setValue("node/nodeCount", nodeCount);
    s.setValue("node/samplePeriodMs", samplePeriodMs);
    s.setValue("node/tempRegAddr", tempRegAddr);
    s.setValue("csv/dir", csvDir);

    s.beginGroup("alarm");
    for (auto it = alarmLow.constBegin(); it != alarmLow.constEnd(); ++it) {
        s.setValue(QString::number(it.key()) + "/low", it.value());
    }
    for (auto it = alarmHigh.constBegin(); it != alarmHigh.constEnd(); ++it) {
        s.setValue(QString::number(it.key()) + "/high", it.value());
    }
    s.endGroup();
}
```

- [ ] **Step 3: 更新 tests/CMakeLists.txt 追加 tst_appconfig**

```cmake
qt_add_test(tst_sample SOURCES tst_sample.cpp)
target_link_libraries(tst_sample PRIVATE Qt6::Core)
target_include_directories(tst_sample PRIVATE ${CMAKE_SOURCE_DIR}/src)

qt_add_test(tst_appconfig SOURCES tst_appconfig.cpp
            ${CMAKE_SOURCE_DIR}/src/AppConfig.cpp)
target_link_libraries(tst_appconfig PRIVATE Qt6::Core)
target_include_directories(tst_appconfig PRIVATE ${CMAKE_SOURCE_DIR}/src)
```

- [ ] **Step 4: 写 tst_appconfig.cpp（失败测试）**

```cpp
#include <QtTest/QtTest>
#include <QSettings>
#include <QCoreApplication>
#include "AppConfig.h"

class TestAppConfig : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();   // 用临时 ini 文件，避免污染真实配置
    void defaultsPersist();
};

void TestAppConfig::initTestCase() {
    // 指向临时 ini
    QString iniPath = QDir::tempPath() + "/tst_appconfig.ini";
    if (QFile::exists(iniPath)) QFile::remove(iniPath);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::tempPath());
    QCoreApplication::setOrganizationName("TestOrg");
    QCoreApplication::setApplicationName("tst_appconfig");
}

void TestAppConfig::defaultsPersist() {
    AppConfig cfg;
    cfg.load();                       // 全用默认值
    cfg.alarmLow[1]  = -5.0;
    cfg.alarmHigh[1] = 55.0;
    cfg.save();

    AppConfig cfg2;
    cfg2.load();
    QCOMPARE(cfg2.alarmLow.value(1),  -5.0);
    QCOMPARE(cfg2.alarmHigh.value(1), 55.0);
    QCOMPARE(cfg2.tempRegAddr, quint16(0x76C1));
    QCOMPARE(cfg2.samplePeriodMs, 2000);
}

QTEST_MAIN(TestAppConfig)
#include "tst_appconfig.moc"
```

> 需在文件顶部加 `#include <QDir>` `#include <QFile>`。

- [ ] **Step 5: 编译并运行测试**

```bash
cmake --build build
ctest --test-dir build -R "tst_sample|tst_appconfig" --output-on-failure
```
Expected: 全部 PASS。

- [ ] **Step 6: 提交**

```bash
git add src/AppConfig.h src/AppConfig.cpp tests
git commit -m "feat: AppConfig 配置持久化"
```

---

## Task 3: CsvWriter CSV 追加写入

**Files:**
- Modify: `src/CsvWriter.h`
- Modify: `src/CsvWriter.cpp`
- Modify: `tests/CMakeLists.txt`
- Create: `tests/tst_csvwriter.cpp`

**Interfaces:**
- Produces: `class CsvWriter`：`bool startSession(const QString &dir, const QDateTime &startTime)`；`void write(const QVector<Sample> &samples)`；`void close()`；`QString currentFile() const`

- [ ] **Step 1: 写 src/CsvWriter.h**

```cpp
#pragma once
#include "Sample.h"
#include <QString>
#include <QFile>
#include <QTextStream>

class CsvWriter {
public:
    // 在 dir 下创建 temp_YYYYMMDD_HHMMSS.csv，写表头（UTF-8 BOM）
    bool startSession(const QString &dir, const class QDateTime &startTime);
    void write(const QVector<Sample> &samples);
    void close();
    QString currentFile() const { return m_file.fileName(); }
    bool isOpen() const { return m_file.isOpen(); }
private:
    QFile m_file;
    QTextStream m_out;
};
```

- [ ] **Step 2: 写 src/CsvWriter.cpp**

```cpp
#include "CsvWriter.h"
#include <QDir>
#include <QDateTime>

bool CsvWriter::startSession(const QString &dir, const QDateTime &startTime) {
    QDir().mkpath(dir);
    QString name = "temp_" + startTime.toString("yyyyMMdd_HHmmss") + ".csv";
    QString path = dir + "/" + name;
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    m_out.setDevice(&m_file);
    m_out.setEncoding(QStringConverter::Utf8);
    m_out.setGenerateByteOrderMark(true);   // UTF-8 BOM，Excel 友好
    m_out << "timestamp,node_id,temp_celsius,raw,online,alarm\n";
    m_out.flush();
    return true;
}

void CsvWriter::write(const QVector<Sample> &samples) {
    if (!m_file.isOpen()) return;
    for (const auto &s : samples) {
        QDateTime dt = QDateTime::fromMSecsSinceEpoch(s.timestampMs);
        m_out << dt.toString("yyyy-MM-dd HH:mm:ss.zzz") << ","
              << s.nodeId << ","
              << QString::number(s.tempCelsius, 'f', 1) << ","
              << s.raw << ","
              << s.online << ","
              << s.alarm << "\n";
    }
    m_out.flush();
}

void CsvWriter::close() {
    if (m_file.isOpen()) {
        m_out.flush();
        m_file.close();
    }
}
```

- [ ] **Step 3: 更新 tests/CMakeLists.txt 追加 tst_csvwriter**

```cmake
qt_add_test(tst_csvwriter SOURCES tst_csvwriter.cpp
            ${CMAKE_SOURCE_DIR}/src/CsvWriter.cpp)
target_link_libraries(tst_csvwriter PRIVATE Qt6::Core)
target_include_directories(tst_csvwriter PRIVATE ${CMAKE_SOURCE_DIR}/src)
```

- [ ] **Step 4: 写 tst_csvwriter.cpp（失败测试）**

```cpp
#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include "CsvWriter.h"

class TestCsvWriter : public QObject {
    Q_OBJECT
private slots:
    void writesHeaderAndRows();
};

void TestCsvWriter::writesHeaderAndRows() {
    QString dir = QDir::tempPath() + "/tst_csv";
    QDir(dir).removeRecursively();

    CsvWriter w;
    QVERIFY(w.startSession(dir, QDateTime(QDate(2026,6,29), QTime(14,30,1))));
    QVERIFY(w.isOpen());

    Sample s1{ QDateTime(QDate(2026,6,29), QTime(14,30,1,123)).toMSecsSinceEpoch(),
               1, 191, 19.1, 1, 0 };
    Sample s2{ s1.timestampMs, 2, 253, 25.3, 1, 0 };
    w.write({s1, s2});
    w.close();

    QFile f(w.currentFile());
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    QString content = QString::fromUtf8(f.readAll());
    QVERIFY(content.startsWith("\xEF\xBB\xBF"));  // BOM
    QVERIFY(content.contains("timestamp,node_id,temp_celsius,raw,online,alarm"));
    QVERIFY(content.contains("2026-06-29 14:30:01.123,1,19.1,191,1,0"));
    QVERIFY(content.contains("2026-06-29 14:30:01.123,2,25.3,253,1,0"));
}

QTEST_MAIN(TestCsvWriter)
#include "tst_csvwriter.moc"
```

- [ ] **Step 5: 编译并运行测试**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```
Expected: 3 个测试套件全 PASS。

- [ ] **Step 6: 提交**

```bash
git add src/CsvWriter.h src/CsvWriter.cpp tests
git commit -m "feat: CsvWriter CSV 追加写入（UTF-8 BOM）"
```

---

## Task 4: ModbusWorker 子线程轮询

**Files:**
- Modify: `src/ModbusWorker.h`
- Modify: `src/ModbusWorker.cpp`

**Interfaces:**
- Consumes: `AppConfig`（串口/节点/寄存器参数）
- Produces: `class ModbusWorker : public QObject`；`void start(const AppConfig &cfg)`；`void stop()`；信号 `void dataReady(QVector<Sample>)`；`void error(const QString &msg)`；`void statusMessage(const QString &msg)`
- 运行在子线程：`MainWindow` 用 `QThread` + `moveToThread` 装载。

- [ ] **Step 1: 写 src/ModbusWorker.h**

```cpp
#pragma once
#include "Sample.h"
#include "AppConfig.h"
#include <QObject>
#include <QTimer>
#include <QVector>

class QModbusRtuSerialMaster;
class QModbusReply;

class ModbusWorker : public QObject {
    Q_OBJECT
public:
    explicit ModbusWorker(QObject *parent = nullptr);
    ~ModbusWorker();
public slots:
    void start(const AppConfig &cfg);
    void stop();
signals:
    void dataReady(QVector<Sample> samples);
    void error(const QString &msg);
    void statusMessage(const QString &msg);
private slots:
    void onTimeout();
    void onReplyFinished();
private:
    QModbusRtuSerialMaster *m_master = nullptr;
    QTimer  m_timer;
    AppConfig m_cfg;
    int   m_failCount = 0;
    bool  m_running = false;
};
```

- [ ] **Step 2: 写 src/ModbusWorker.cpp**

```cpp
#include "ModbusWorker.h"
#include <QModbusRtuSerialMaster>
#include <QModbusReply>
#include <QModbusDataUnit>
#include <QSerialPort>
#include <QDateTime>

ModbusWorker::ModbusWorker(QObject *parent) : QObject(parent) {
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &ModbusWorker::onTimeout);
}

ModbusWorker::~ModbusWorker() {
    stop();
    if (m_master) m_master->deleteLater();
}

void ModbusWorker::start(const AppConfig &cfg) {
    m_cfg = cfg;
    if (m_master) { m_master->deleteLater(); m_master = nullptr; }

    m_master = new QModbusRtuSerialMaster(this);
    m_master->setConnectionParameter(QModbusDevice::SerialPortNameParameter, cfg.portName);
    m_master->setConnectionParameter(QModbusDevice::SerialBaudRateParameter, cfg.baudRate);
    m_master->setConnectionParameter(QModbusDevice::SerialDataBitsParameter, cfg.dataBits);
    m_master->setConnectionParameter(QModbusDevice::SerialStopBitsParameter,
        cfg.stopBits == 1 ? QSerialPort::OneStop : QSerialPort::TwoStop);
    QSerialPort::Parity p = QSerialPort::NoParity;
    if (cfg.parity == 1) p = QSerialPort::OddParity;
    else if (cfg.parity == 2) p = QSerialPort::EvenParity;
    m_master->setConnectionParameter(QModbusDevice::SerialParityParameter, p);
    m_master->setTimeout(1000);

    if (!m_master->connectDevice()) {
        emit error(QString("串口打开失败: %1").arg(m_master->errorString()));
        m_master->deleteLater();
        m_master = nullptr;
        return;
    }
    m_running = true;
    m_failCount = 0;
    emit statusMessage("采集中");
    m_timer.start(m_cfg.samplePeriodMs);
    onTimeout();   // 立即采集一次
}

void ModbusWorker::stop() {
    m_timer.stop();
    m_running = false;
    if (m_master) m_master->disconnectDevice();
    emit statusMessage("已停止");
}

void ModbusWorker::onTimeout() {
    if (!m_master || !m_running) return;
    // 功能码 0x04 读输入寄存器
    QModbusDataUnit unit(QModbusDataUnit::InputRegisters,
                         m_cfg.tempRegAddr, m_cfg.nodeCount);
    QModbusReply *reply = m_master->sendReadRequest(unit, m_cfg.slaveAddr);
    if (!reply) {
        if (++m_failCount >= 3) emit error("通讯异常: 请求发送失败");
        return;
    }
    connect(reply, &QModbusReply::finished, this, &ModbusWorker::onReplyFinished);
}

void ModbusWorker::onReplyFinished() {
    QModbusReply *reply = qobject_cast<QModbusReply *>(sender());
    if (!reply) return;
    reply->deleteLater();

    if (reply->error() != QModbusDevice::NoError) {
        if (++m_failCount >= 3)
            emit error(QString("通讯异常: %1").arg(reply->errorString()));
        return;
    }
    m_failCount = 0;

    const QModbusDataUnit unit = reply->result();
    QVector<Sample> samples;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int i = 0; i < unit.valueCount() && i < m_cfg.nodeCount; ++i) {
        quint16 raw = unit.value(i);
        Sample s;
        s.timestampMs = now;
        s.nodeId      = m_cfg.startNodeId + i;
        s.raw         = raw;
        s.tempCelsius = parseTempCelsius(raw);
        s.online      = 1;
        s.alarm       = checkAlarm(s.tempCelsius,
                                   m_cfg.alarmLow.value(s.nodeId, -10.0),
                                   m_cfg.alarmHigh.value(s.nodeId, 60.0));
        samples.append(s);
    }
    emit dataReady(samples);
}
```

- [ ] **Step 3: 编译验证**

```bash
cmake --build build
```
Expected: 编译通过（ModbusWorker 依赖硬件，无单元测试，靠编译 + 后续手动测试）。

- [ ] **Step 4: 提交**

```bash
git add src/ModbusWorker.h src/ModbusWorker.cpp
git commit -m "feat: ModbusWorker 子线程轮询温度1寄存器"
```

---

## Task 5: ChartManager 实时曲线管理

**Files:**
- Modify: `src/ChartManager.h`
- Modify: `src/ChartManager.cpp`

**Interfaces:**
- Consumes: `Sample`
- Produces: `class ChartManager`：`QChart* chart()`；`void setupNodes(int startId, int count)`；`void append(const QVector<Sample> &samples)`；每节点保留最近 30 分钟数据。

- [ ] **Step 1: 写 src/ChartManager.h**

```cpp
#pragma once
#include "Sample.h"
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QHash>
#include <QDateTimeAxis>
#include <QValueAxis>

class ChartManager {
public:
    ChartManager();
    QtCharts::QChart* chart() { return m_chart; }
    void setupNodes(int startId, int count);
    void append(const QVector<Sample> &samples);
    void clear();
private:
    QtCharts::QChart *m_chart;
    QtCharts::QDateTimeAxis *m_axisX;
    QtCharts::QValueAxis    *m_axisY;
    QHash<int, QtCharts::QLineSeries*> m_series;  // key=nodeId
    int m_startId = 1;
    int m_count   = 0;
    static constexpr int kMaxPointsPerSeries = 900; // 30min @ 2s
};
```

- [ ] **Step 2: 写 src/ChartManager.cpp**

```cpp
#include "ChartManager.h"
#include <QDateTime>

ChartManager::ChartManager() {
    m_chart = new QtCharts::QChart();
    m_chart->setTitle("实时温度曲线");
    m_chart->legend()->setVisible(true);

    m_axisX = new QtCharts::QDateTimeAxis();
    m_axisX->setFormat("HH:mm:ss");
    m_axisX->setTitleText("时间");
    m_chart->addAxis(m_axisX, Qt::AlignBottom);

    m_axisY = new QtCharts::QValueAxis();
    m_axisY->setTitleText("温度 (℃)");
    m_axisY->setRange(-20, 80);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);
}

void ChartManager::setupNodes(int startId, int count) {
    // 清理旧曲线
    for (auto s : m_series) { m_chart->removeSeries(s); s->deleteLater(); }
    m_series.clear();
    m_startId = startId;
    m_count   = count;
    for (int i = 0; i < count; ++i) {
        int id = startId + i;
        auto *s = new QtCharts::QLineSeries();
        s->setName(QString("ID%1").arg(id));
        m_chart->addSeries(s);
        s->attachAxis(m_axisX);
        s->attachAxis(m_axisY);
        m_series.insert(id, s);
    }
}

void ChartManager::append(const QVector<Sample> &samples) {
    for (const auto &s : samples) {
        auto it = m_series.find(s.nodeId);
        if (it == m_series.end()) continue;
        (*it)->append(s.timestampMs, s.tempCelsius);
        // 限制点数
        while ((*it)->count() > kMaxPointsPerSeries)
            (*it)->remove(0);
    }
    // 自适应 X 范围（最近 30 分钟）
    if (!samples.isEmpty()) {
        qint64 now = samples.last().timestampMs;
        m_axisX->setRange(QDateTime::fromMSecsSinceEpoch(now - 30*60*1000),
                          QDateTime::fromMSecsSinceEpoch(now));
    }
}

void ChartManager::clear() {
    for (auto s : m_series) s->clear();
}
```

- [ ] **Step 3: 编译验证**

```bash
cmake --build build
```
Expected: 编译通过。

- [ ] **Step 4: 提交**

```bash
git add src/ChartManager.h src/ChartManager.cpp
git commit -m "feat: ChartManager 实时温度曲线"
```

---

## Task 6: MainWindow 主窗口 UI + 信号槽组装

**Files:**
- Modify: `src/MainWindow.h`
- Modify: `src/MainWindow.cpp`

**Interfaces:**
- Consumes: `AppConfig, ModbusWorker, CsvWriter, ChartManager, Sample`
- Produces: `class MainWindow : public QMainWindow`，组装 UI、连接信号槽、管理采集生命周期。

- [ ] **Step 1: 写 src/MainWindow.h**

```cpp
#pragma once
#include "AppConfig.h"
#include "CsvWriter.h"
#include "ChartManager.h"
#include <QMainWindow>
#include <QThread>
#include <QHash>
class ModbusWorker;
class QComboBox;
class QSpinBox;
class QGridLayout;
class QTableWidget;
class QLabel;
class QPushButton;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
private slots:
    void onStart();
    void onStop();
    void onDataReady(QVector<Sample> samples);
    void onError(const QString &msg);
    void onStatus(const QString &msg);
    void onChooseCsvDir();
private:
    void buildUi();
    void loadConfig();
    void applyConfigToUi();
    void applyUiToConfig();
    void rebuildCards();

    // 配置面板控件
    QComboBox *m_portCombo;
    QComboBox *m_baudCombo;
    QSpinBox  *m_slaveSpin;
    QSpinBox  *m_startIdSpin;
    QSpinBox  *m_nodeCountSpin;
    QSpinBox  *m_periodSpin;
    QLabel    *m_csvDirLabel;
    QPushButton *m_csvBtn;
    QPushButton *m_startBtn;
    QPushButton *m_stopBtn;

    // 主区域
    QWidget       *m_cardContainer;
    QGridLayout   *m_cardLayout;
    QHash<int, QLabel*> m_cardLabels;   // nodeId -> 大字号温度标签

    ChartManager   m_chartMgr;
    QTableWidget  *m_table;

    AppConfig  m_cfg;
    CsvWriter  m_csv;
    ModbusWorker *m_worker = nullptr;
    QThread   *m_thread = nullptr;
};
```

- [ ] **Step 2: 写 src/MainWindow.cpp（UI 构建 + 信号槽）**

```cpp
#include "MainWindow.h"
#include "ModbusWorker.h"
#include "Sample.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QStatusBar>
#include <QFileDialog>
#include <QDateTime>
#include <QSerialPortInfo>
#include <QChartView>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("LoRa 温度监测");
    resize(1100, 700);
    buildUi();
    loadConfig();
    applyConfigToUi();
    rebuildCards();
    m_chartMgr.setupNodes(m_cfg.startNodeId, m_cfg.nodeCount);

    auto *chartView = new QtCharts::QChartView(m_chartMgr.chart());
    chartView->setRenderHint(QPainter::Antialiasing);
    // chartView 通过布局加入（在 buildUi 中占位替换）
}

MainWindow::~MainWindow() {
    if (m_thread) {
        if (m_worker) QMetaObject::invokeMethod(m_worker, &ModbusWorker::stop);
        m_thread->quit();
        m_thread->wait();
    }
    m_csv.close();
    m_cfg.save();
}

void MainWindow::buildUi() {
    auto *central = new QWidget;
    auto *mainLay = new QHBoxLayout(central);

    // ===== 左侧配置面板 =====
    auto *panel = new QGroupBox("配置");
    auto *form = new QFormLayout(panel);
    m_portCombo = new QComboBox;
    for (const auto &info : QSerialPortInfo::availablePorts())
        m_portCombo->addItem(info.portName());
    m_baudCombo = new QComboBox;
    m_baudCombo->addItems({"9600","19200","38400","57600","115200"});
    m_slaveSpin = new QSpinBox; m_slaveSpin->setRange(1, 247);
    m_startIdSpin = new QSpinBox; m_startIdSpin->setRange(1, 200);
    m_nodeCountSpin = new QSpinBox; m_nodeCountSpin->setRange(1, 16);
    m_periodSpin = new QSpinBox; m_periodSpin->setRange(500, 60000); m_periodSpin->setSingleStep(500);
    m_csvDirLabel = new QLabel;
    m_csvBtn = new QPushButton("选择...");
    m_startBtn = new QPushButton("开始");
    m_stopBtn = new QPushButton("停止");
    m_stopBtn->setEnabled(false);

    form->addRow("串口", m_portCombo);
    form->addRow("波特率", m_baudCombo);
    form->addRow("从机地址", m_slaveSpin);
    form->addRow("起始ID", m_startIdSpin);
    form->addRow("节点数", m_nodeCountSpin);
    form->addRow("采样周期(ms)", m_periodSpin);
    form->addRow("CSV目录", m_csvDirLabel);
    form->addRow("", m_csvBtn);
    form->addRow(m_startBtn);
    form->addRow(m_stopBtn);

    // ===== 右侧主区域 =====
    auto *right = new QWidget;
    auto *rightLay = new QVBoxLayout(right);
    m_cardContainer = new QWidget;
    m_cardLayout = new QGridLayout(m_cardContainer);
    auto *chartView = new QtCharts::QChartView(m_chartMgr.chart());
    chartView->setRenderHint(QPainter::Antialiasing);
    chartView->setMinimumHeight(250);
    m_table = new QTableWidget(0, 5);
    m_table->setHorizontalHeaderLabels({"时间","节点","温度(℃)","原始值","报警"});
    m_table->horizontalHeader()->setStretchLastSection(true);

    rightLay->addWidget(m_cardContainer);
    rightLay->addWidget(chartView, 2);
    rightLay->addWidget(m_table, 1);

    mainLay->addWidget(panel);
    mainLay->addWidget(right, 1);
    setCentralWidget(central);
    statusBar()->showMessage("就绪");

    connect(m_startBtn, &QPushButton::clicked, this, &MainWindow::onStart);
    connect(m_stopBtn,  &QPushButton::clicked, this, &MainWindow::onStop);
    connect(m_csvBtn,   &QPushButton::clicked, this, &MainWindow::onChooseCsvDir);
    connect(m_startIdSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int){
        applyUiToConfig(); rebuildCards(); m_chartMgr.setupNodes(m_cfg.startNodeId, m_cfg.nodeCount);
    });
    connect(m_nodeCountSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int){
        applyUiToConfig(); rebuildCards(); m_chartMgr.setupNodes(m_cfg.startNodeId, m_cfg.nodeCount);
    });
}

void MainWindow::loadConfig() {
    m_cfg.load();
}

void MainWindow::applyConfigToUi() {
    m_portCombo->setCurrentText(m_cfg.portName);
    m_baudCombo->setCurrentText(QString::number(m_cfg.baudRate));
    m_slaveSpin->setValue(m_cfg.slaveAddr);
    m_startIdSpin->setValue(m_cfg.startNodeId);
    m_nodeCountSpin->setValue(m_cfg.nodeCount);
    m_periodSpin->setValue(m_cfg.samplePeriodMs);
    m_csvDirLabel->setText(m_cfg.resolvedCsvDir());
}

void MainWindow::applyUiToConfig() {
    m_cfg.portName       = m_portCombo->currentText();
    m_cfg.baudRate       = m_baudCombo->currentText().toInt();
    m_cfg.slaveAddr      = m_slaveSpin->value();
    m_cfg.startNodeId    = m_startIdSpin->value();
    m_cfg.nodeCount      = m_nodeCountSpin->value();
    m_cfg.samplePeriodMs = m_periodSpin->value();
    m_cfg.csvDir         = m_csvDirLabel->text();
    m_cfg.ensureDefaults();
}

void MainWindow::rebuildCards() {
    // 清空旧卡片
    while (m_cardLayout->count()) {
        auto *item = m_cardLayout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
    }
    m_cardLabels.clear();
    int cols = 4;
    for (int i = 0; i < m_cfg.nodeCount; ++i) {
        int id = m_cfg.startNodeId + i;
        auto *card = new QFrame;
        card->setFrameShape(QFrame::Box);
        card->setStyleSheet("QFrame{background:#f5f5f5;border:1px solid #ccc;border-radius:6px;}");
        auto *l = new QVBoxLayout(card);
        auto *title = new QLabel(QString("ID%1").arg(id));
        title->setAlignment(Qt::AlignCenter);
        auto *val = new QLabel("--");
        val->setAlignment(Qt::AlignCenter);
        QFont f = val->font(); f.setPointSize(20); f.setBold(true); val->setFont(f);
        l->addWidget(title);
        l->addWidget(val);
        m_cardLabels.insert(id, val);
        m_cardLayout->addWidget(card, i / cols, i % cols);
    }
}

void MainWindow::onChooseCsvDir() {
    QString dir = QFileDialog::getExistingDirectory(this, "选择 CSV 保存目录", m_csvDirLabel->text());
    if (!dir.isEmpty()) m_csvDirLabel->setText(dir);
}

void MainWindow::onStart() {
    applyUiToConfig();
    m_cfg.save();
    if (m_cfg.portName.isEmpty()) {
        statusBar()->showMessage("请选择串口", 5000);
        return;
    }
    // 新建 CSV 会话
    if (!m_csv.startSession(m_cfg.resolvedCsvDir(), QDateTime::currentDateTime())) {
        statusBar()->showMessage("CSV 文件创建失败", 5000);
        return;
    }
    m_chartMgr.clear();

    // 启动子线程
    m_thread = new QThread(this);
    m_worker = new ModbusWorker;
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &ModbusWorker::dataReady, this, &MainWindow::onDataReady);
    connect(m_worker, &ModbusWorker::error, this, &MainWindow::onError);
    connect(m_worker, &ModbusWorker::statusMessage, this, &MainWindow::onStatus);
    m_thread->start();
    QMetaObject::invokeMethod(m_worker, "start", Qt::QueuedConnection,
                              Q_ARG(AppConfig, m_cfg));
    m_startBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);
}

void MainWindow::onStop() {
    if (m_worker) QMetaObject::invokeMethod(m_worker, &ModbusWorker::stop, Qt::QueuedConnection);
    if (m_thread) { m_thread->quit(); m_thread->wait(); m_thread->deleteLater(); m_thread = nullptr; m_worker = nullptr; }
    m_csv.close();
    m_startBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);
    statusBar()->showMessage("已停止");
}

void MainWindow::onDataReady(QVector<Sample> samples) {
    m_csv.write(samples);
    m_chartMgr.append(samples);
    for (const auto &s : samples) {
        auto it = m_cardLabels.find(s.nodeId);
        if (it != m_cardLabels.end()) {
            (*it)->setText(QString::number(s.tempCelsius, 'f', 1) + " ℃");
            if (s.alarm == 1)      (*it)->setStyleSheet("color:white;background:#c0392b;");
            else if (s.alarm == -1)(*it)->setStyleSheet("color:white;background:#2980b9;");
            else                   (*it)->setStyleSheet("color:#222;background:#f5f5f5;");
        }
        // 表格插行（顶部插入，最新在上）
        int row = 0;
        m_table->insertRow(row);
        QDateTime dt = QDateTime::fromMSecsSinceEpoch(s.timestampMs);
        m_table->setItem(row, 0, new QTableWidgetItem(dt.toString("HH:mm:ss.zzz")));
        m_table->setItem(row, 1, new QTableWidgetItem(QString::number(s.nodeId)));
        m_table->setItem(row, 2, new QTableWidgetItem(QString::number(s.tempCelsius, 'f', 1)));
        m_table->setItem(row, 3, new QTableWidgetItem(QString::number(s.raw)));
        m_table->setItem(row, 4, new QTableWidgetItem(
            s.alarm==1?"超上限":(s.alarm==-1?"超下限":"正常")));
        // 限制行数
        while (m_table->rowCount() > 500) m_table->removeRow(m_table->rowCount()-1);
    }
    statusBar()->showMessage(QString("采集中 | 上次: %1 | 文件: %2")
        .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
        .arg(QFileInfo(m_csv.currentFile()).fileName()));
}

void MainWindow::onError(const QString &msg) {
    statusBar()->showMessage("错误: " + msg, 10000);
}

void MainWindow::onStatus(const QString &msg) {
    statusBar()->showMessage(msg);
}
```

> 头部需 `#include <QFileInfo>` `<QFrame>`。

- [ ] **Step 3: 编译验证**

```bash
cmake --build build
```
Expected: 编译通过。若有未识别符号，按提示补 `#include`。

- [ ] **Step 4: 提交**

```bash
git add src/MainWindow.h src/MainWindow.cpp
git commit -m "feat: MainWindow 主窗口 UI 与信号槽组装"
```

---

## Task 7: main.cpp 集成 + 跨平台运行验证

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `MainWindow`

- [ ] **Step 1: 写 src/main.cpp**

```cpp
#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setOrganizationName("LoRaTemperature");
    app.setApplicationName("LoRaTemperature");

    MainWindow w;
    w.show();
    return app.exec();
}
```

- [ ] **Step 2: 完整编译并运行单元测试**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```
Expected: tst_sample / tst_appconfig / tst_csvwriter 全 PASS，应用编译成功。

- [ ] **Step 3: 运行应用（Linux 手动验证）**

```bash
./build/src/LoRaTemperature
```
Expected: 窗口打开，串口下拉枚举系统串口，配置面板可用，点"开始"前未连硬件会提示串口错误。

- [ ] **Step 4: 硬件联调验证（有硬件时）**

1. USB转RS485 接集中器，选择对应串口
2. 点"开始"，观察 ID1/ID2 卡片每 2 秒更新温度
3. 检查 `data/temp_YYYYMMDD_HHMMSS.csv` 内容与卡片一致
4. 用手捂/冰袋触发超限，卡片变红/蓝，状态栏提示
5. 点"停止"，CSV 文件关闭，Excel 打开无乱码

- [ ] **Step 5: 提交**

```bash
git add src/main.cpp
git commit -m "feat: 集成 main.cpp 完成应用"
```

---

## Self-Review

**1. Spec 覆盖：**
- 串口 Modbus RTU 读温度1 (0x76C1) → Task 4 ✓
- 2 秒采样、ID1+ID2 → AppConfig 默认 + Task 4 ✓
- 大端/int16补码/÷10 解析 → Sample.h + Task 1 测试 ✓
- 当前值卡片 + 实时曲线 + 表格 → Task 6 ✓
- 自动 CSV（UTF-8 BOM、按会话）→ Task 3 ✓
- 上下限报警变色 → Task 6 ✓
- 配置持久化 → Task 2 ✓
- 跨平台编译 → Task 7 ✓

**2. 占位符扫描：** 无 TBD/TODO，所有代码步骤含完整代码。

**3. 类型一致性：**
- `Sample` 字段在 Task 1/3/4/6 一致 ✓
- `parseTempCelsius` / `checkAlarm` 在 Task 1 定义，Task 4 调用 ✓
- `ModbusWorker::start(const AppConfig&)` 在 Task 4 定义，Task 6 调用 ✓
- `CsvWriter::startSession(dir, QDateTime)` 在 Task 3 定义，Task 6 调用 ✓
- `ChartManager::setupNodes/append` 在 Task 5 定义，Task 6 调用 ✓

无矛盾。
