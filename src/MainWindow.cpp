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
#include <QCheckBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QStatusBar>
#include <QFileDialog>
#include <QDateTime>
#include <QSerialPortInfo>
#include <QChartView>
#include <QFileInfo>
#include <QFrame>
#include <QMetaType>
#include <QColor>
#include <QTimer>
#include <QtMath>
#include <QPainter>
#include <QPainterPath>
#include <random>
#include <QPlainTextEdit>
#include <QStringList>

// 生成温度计图标作为窗口 logo
static QIcon makeThermometerIcon() {
    const int sz = 64;
    QPixmap pix(sz, sz);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    // 温度计球部（红色圆）
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#e74c3c"));
    p.drawEllipse(QPointF(sz/2.0, sz*0.82), sz*0.18, sz*0.18);
    // 玻璃管（白色描边）
    QPainterPath tube;
    qreal tubeW = sz*0.14;
    qreal tubeX = sz/2.0 - tubeW/2.0;
    qreal tubeTop = sz*0.12;
    qreal tubeBottom = sz*0.74;
    tube.addRoundedRect(QRectF(tubeX, tubeTop, tubeW, tubeBottom-tubeTop),
                        tubeW/2.0, tubeW/2.0);
    p.setBrush(QColor("#ecf0f1"));
    p.setPen(QPen(QColor("#bdc3c7"), 1.5));
    p.drawPath(tube);
    // 水银柱（红色，从球部向上填充约 60%）
    QPainterPath mercury;
    qreal mW = tubeW*0.6;
    qreal mX = sz/2.0 - mW/2.0;
    qreal mTop = sz*0.35;
    qreal mBottom = sz*0.78;
    mercury.addRoundedRect(QRectF(mX, mTop, mW, mBottom-mTop),
                           mW/2.0, mW/2.0);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#e74c3c"));
    p.drawPath(mercury);
    // 顶部刻度线
    p.setPen(QPen(QColor("#7f8c8d"), 1.0));
    for (int i = 0; i < 4; ++i) {
        qreal y = tubeTop + (tubeBottom-tubeTop)*(i+1)/5.0;
        p.drawLine(QPointF(tubeX+tubeW+2, y), QPointF(tubeX+tubeW+7, y));
    }
    return QIcon(pix);
}

// ID 颜色映射（最多 8 个，与 ChartManager 共用同一套颜色）
const QColor MainWindow::kIdColors[] = {
    ChartManager::kColors[0],
    ChartManager::kColors[1],
    ChartManager::kColors[2],
    ChartManager::kColors[3],
    ChartManager::kColors[4],
    ChartManager::kColors[5],
    ChartManager::kColors[6],
    ChartManager::kColors[7],
};

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    // 跨线程信号槽需要注册自定义类型
    qRegisterMetaType<QVector<Sample>>("QVector<Sample>");
    qRegisterMetaType<AppConfig>("AppConfig");

    setWindowTitle("LoRa 温度监测");
    setWindowIcon(makeThermometerIcon());
    resize(1200, 880);
    buildUi();
    loadConfig();
    applyConfigToUi();
    rebuildCards();
    rebuildIdSelectors();
    m_chartMgr.setupNodes(m_cfg.startNodeId, m_cfg.nodeCount);
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
    // 外层垂直布局：上=主面板，下=log框
    auto *outerLay = new QVBoxLayout(central);
    outerLay->setContentsMargins(0, 0, 0, 0);
    outerLay->setSpacing(4);

    // 上方主面板（左右布局）
    auto *mainPanel = new QWidget;
    auto *mainLay = new QHBoxLayout(mainPanel);
    mainLay->setContentsMargins(0, 0, 0, 0);

    // ===== 左侧面板（配置 + 卡片） =====
    auto *leftPanel = new QWidget;
    auto *leftLay = new QVBoxLayout(leftPanel);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(8);

    // --- 配置区域 ---
    auto *configGroup = new QGroupBox("配置");
    auto *form = new QFormLayout(configGroup);
    m_portCombo = new QComboBox;
    m_refreshPortBtn = new QPushButton("刷新");
    for (const auto &info : QSerialPortInfo::availablePorts())
        m_portCombo->addItem(info.portName());
    // 串口选择行：下拉框 + 刷新按钮
    auto *portRow = new QWidget;
    auto *portLay = new QHBoxLayout(portRow);
    portLay->setContentsMargins(0, 0, 0, 0);
    portLay->addWidget(m_portCombo, 1);
    portLay->addWidget(m_refreshPortBtn);
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
    auto *simBtn = new QPushButton("模拟模式");

    form->addRow("串口", portRow);
    form->addRow("波特率", m_baudCombo);
    form->addRow("从机地址", m_slaveSpin);
    form->addRow("起始ID", m_startIdSpin);
    form->addRow("节点数", m_nodeCountSpin);
    form->addRow("采样周期(ms)", m_periodSpin);
    form->addRow("CSV目录", m_csvDirLabel);
    form->addRow("", m_csvBtn);
    form->addRow(m_startBtn);
    form->addRow(m_stopBtn);
    form->addRow(simBtn);

    // --- 温度卡片区域（网格布局，8 个卡片） ---
    auto *cardGroup = new QGroupBox("当前温度");
    m_cardLayout = new QGridLayout(cardGroup);
    m_cardLayout->setContentsMargins(4, 4, 4, 4);
    m_cardLayout->setSpacing(4);
    m_cardContainer = new QWidget;

    leftLay->addWidget(configGroup, 0);
    leftLay->addWidget(cardGroup, 1);  // 卡片占据左侧剩余空间

    // ===== 右侧面板（曲线 + ID选择器 + 表格） =====
    auto *rightPanel = new QWidget;
    auto *rightLay = new QVBoxLayout(rightPanel);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(4);

    auto *chartView = new QChartView(m_chartMgr.chart());
    chartView->setRenderHint(QPainter::Antialiasing);
    chartView->setMinimumHeight(200);

    // --- ID 选择器区域（放在曲线下方） ---
    auto *idGroup = new QGroupBox("曲线显示");
    m_idSelectorLayout = new QGridLayout(idGroup);
    m_idSelectorLayout->setContentsMargins(4, 4, 4, 4);
    m_idSelectorLayout->setSpacing(4);
    m_idSelectorPanel = new QWidget;

    m_table = new QTableWidget(0, 5);
    m_table->setHorizontalHeaderLabels({"时间","节点","温度(℃)","原始值","报警"});
    m_table->horizontalHeader()->setStretchLastSection(true);

    // 图表:表格 = 3:2，即图表是表格的 1.5 倍（满足 1.2~1.8 范围）
    rightLay->addWidget(chartView, 3);   // 曲线占 3 份
    rightLay->addWidget(idGroup, 0);     // ID选择器固定高度
    rightLay->addWidget(m_table, 2);     // 表格占 2 份

    // ===== 组装主面板 =====
    mainLay->addWidget(leftPanel, 0);   // 左侧不拉伸
    mainLay->addWidget(rightPanel, 1);  // 右侧拉伸

    // ===== 底部 log 框（宽度占整个应用） =====
    auto *logGroup = new QGroupBox("日志");
    auto *logLay = new QVBoxLayout(logGroup);
    logLay->setContentsMargins(4, 4, 4, 4);
    m_logBox = new QPlainTextEdit;
    m_logBox->setReadOnly(true);
    m_logBox->setMaximumBlockCount(500);  // 最多 500 行
    m_logBox->setFixedHeight(120);        // 固定高度
    QFont logFont("Monospace");
    logFont.setStyleHint(QFont::Monospace);
    m_logBox->setFont(logFont);
    logLay->addWidget(m_logBox);

    outerLay->addWidget(mainPanel, 1);   // 主面板拉伸
    outerLay->addWidget(logGroup, 0);    // log框固定高度
    setCentralWidget(central);
    statusBar()->showMessage("就绪");
    appendLog("应用启动");

    // 信号槽连接
    connect(m_startBtn, &QPushButton::clicked, this, &MainWindow::onStart);
    connect(m_stopBtn,  &QPushButton::clicked, this, &MainWindow::onStop);
    connect(m_csvBtn,   &QPushButton::clicked, this, &MainWindow::onChooseCsvDir);
    // 刷新串口列表
    connect(m_refreshPortBtn, &QPushButton::clicked, this, [this]() {
        QString cur = m_portCombo->currentText();
        m_portCombo->clear();
        for (const auto &info : QSerialPortInfo::availablePorts())
            m_portCombo->addItem(info.portName());
        // 尝试恢复之前选中的串口
        int idx = m_portCombo->findText(cur);
        if (idx >= 0) m_portCombo->setCurrentIndex(idx);
        appendLog(QString("串口列表已刷新，共 %1 个").arg(m_portCombo->count()));
    });
    connect(simBtn,     &QPushButton::clicked, this, [this, simBtn]() {
        if (m_simRunning) {
            onSimStop();
            simBtn->setText("模拟模式");
        } else {
            onSimStart();
            simBtn->setText("停止模拟");
        }
    });
    // 采集周期变更：运行中实时生效
    connect(m_periodSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int ms) {
        if (m_simRunning && m_simTimer) {
            m_simTimer->start(ms);   // 模拟模式：直接重启定时器
            appendLog(QString("采集周期已更新为 %1 ms").arg(ms));
        } else if (m_worker) {
            // 实际采集模式：转发给子线程
            QMetaObject::invokeMethod(m_worker, "setSamplePeriod", Qt::QueuedConnection, Q_ARG(int, ms));
            appendLog(QString("采集周期已更新为 %1 ms").arg(ms));
        }
    });
    connect(m_startIdSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int){
        applyUiToConfig(); rebuildCards(); rebuildIdSelectors();
        m_chartMgr.setupNodes(m_cfg.startNodeId, m_cfg.nodeCount);
    });
    connect(m_nodeCountSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int){
        applyUiToConfig(); rebuildCards(); rebuildIdSelectors();
        m_chartMgr.setupNodes(m_cfg.startNodeId, m_cfg.nodeCount);
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

    const int cols = 2;  // 每行 2 个卡片
    const int totalCards = 8;  // 始终显示 8 个卡片
    for (int i = 0; i < totalCards; ++i) {
        int id = m_cfg.startNodeId + i;
        auto *card = new QFrame;
        card->setFrameShape(QFrame::Box);
        // 超出节点数的卡片用更暗的背景表示无数据
        bool active = (i < m_cfg.nodeCount);
        QString bg = active ? "#f5f5f5" : "#ececec";
        card->setStyleSheet(QString("QFrame{background:%1;border:1px solid #ccc;border-radius:6px;}").arg(bg));
        auto *l = new QVBoxLayout(card);
        l->setContentsMargins(4, 4, 4, 4);
        auto *title = new QLabel(QString("ID%1").arg(id));
        title->setAlignment(Qt::AlignCenter);
        auto *val = new QLabel(active ? "--" : "无");
        val->setAlignment(Qt::AlignCenter);
        if (!active) val->setStyleSheet("color:#999;");
        QFont f = val->font(); f.setPointSize(18); f.setBold(true); val->setFont(f);
        l->addWidget(title);
        l->addWidget(val);
        m_cardLabels.insert(id, val);
        m_cardLayout->addWidget(card, i / cols, i % cols);
    }
}

void MainWindow::rebuildIdSelectors() {
    // 清空旧选择器
    while (m_idSelectorLayout->count()) {
        auto *item = m_idSelectorLayout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
    }
    m_idToggles.clear();

    const int cols = 4;  // 每行 4 个，紧凑显示
    for (int i = 0; i < kMaxSelectableIds; ++i) {
        int id = m_cfg.startNodeId + i;
        auto *cb = new QCheckBox(QString("ID%1").arg(id));
        cb->setChecked(i < m_cfg.nodeCount);  // 默认勾选当前节点数

        // 颜色色块（使用与曲线相同的颜色）
        auto *colorLabel = new QLabel;
        colorLabel->setFixedSize(16, 16);
        colorLabel->setStyleSheet(QString("background-color: %1; border: 1px solid #999; border-radius: 3px;")
            .arg(kIdColors[i % 8].name()));

        // 布局：色块 + 复选框
        auto *rowWidget = new QWidget;
        auto *rowLay = new QHBoxLayout(rowWidget);
        rowLay->setContentsMargins(0, 0, 0, 0);
        rowLay->setSpacing(4);
        rowLay->addWidget(colorLabel);
        rowLay->addWidget(cb);
        rowLay->addStretch();

        m_idToggles.insert(id, cb);
        m_idSelectorLayout->addWidget(rowWidget, i / cols, i % cols);

        connect(cb, &QCheckBox::toggled, this, &MainWindow::onIdToggled);
    }
}

void MainWindow::onIdToggled(bool checked) {
    auto *cb = qobject_cast<QCheckBox*>(sender());
    if (!cb) return;

    // 找到对应的 ID
    for (auto it = m_idToggles.begin(); it != m_idToggles.end(); ++it) {
        if (it.value() == cb) {
            m_chartMgr.setVisible(it.key(), checked);
            appendLog(QString("曲线 %1 ID%2")
                .arg(checked ? "显示" : "隐藏").arg(it.key()));
            break;
        }
    }
}

void MainWindow::onChooseCsvDir() {
    QString dir = QFileDialog::getExistingDirectory(this, "选择 CSV 保存目录", m_csvDirLabel->text());
    if (!dir.isEmpty()) {
        m_csvDirLabel->setText(dir);
        appendLog("CSV 目录已更改为: " + dir);
    }
}

void MainWindow::onStart() {
    applyUiToConfig();
    m_cfg.save();
    if (m_cfg.portName.isEmpty()) {
        statusBar()->showMessage("请选择串口", 5000);
        appendLog("[警告] 未选择串口");
        return;
    }
    // 新建 CSV 会话
    if (!m_csv.startSession(m_cfg.resolvedCsvDir(), QDateTime::currentDateTime())) {
        statusBar()->showMessage("CSV 文件创建失败", 5000);
        appendLog("[错误] CSV 文件创建失败");
        return;
    }
    m_chartMgr.clear();
    appendLog(QString("开始采集 | 串口=%1 波特率=%2 从机=%3 节点ID%4~ID%5 周期=%6ms")
        .arg(m_cfg.portName).arg(m_cfg.baudRate).arg(m_cfg.slaveAddr)
        .arg(m_cfg.startNodeId).arg(m_cfg.startNodeId + m_cfg.nodeCount - 1)
        .arg(m_cfg.samplePeriodMs));
    appendLog(QString("CSV 文件: %1").arg(QFileInfo(m_csv.currentFile()).fileName()));

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
    appendLog("采集已停止，CSV 文件已关闭");
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
        // 报警日志
        if (s.alarm == 1) {
            appendLog(QString("[报警] ID%1 超上限: %2℃").arg(s.nodeId).arg(QString::number(s.tempCelsius, 'f', 1)));
        } else if (s.alarm == -1) {
            appendLog(QString("[报警] ID%1 超下限: %2℃").arg(s.nodeId).arg(QString::number(s.tempCelsius, 'f', 1)));
        }
        // 表格插行（顶部插入，最新在上）
        int row = 0;
        m_table->insertRow(row);
        QDateTime dt = QDateTime::fromMSecsSinceEpoch(s.timestampMs);
        m_table->setItem(row, 0, new QTableWidgetItem(dt.toString("HH:mm:ss.zzz")));
        m_table->setItem(row, 1, new QTableWidgetItem(QString::number(s.nodeId)));
        m_table->setItem(row, 2, new QTableWidgetItem(QString::number(s.tempCelsius, 'f', 1)));
        m_table->setItem(row, 3, new QTableWidgetItem(
            QString("0x%1").arg(s.raw, 4, 16, QChar('0')).toUpper()));
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
    appendLog("[错误] " + msg);
}

void MainWindow::onStatus(const QString &msg) {
    statusBar()->showMessage(msg);
    appendLog(msg);
}

void MainWindow::appendLog(const QString &msg) {
    if (!m_logBox) return;
    QString line = QDateTime::currentDateTime().toString("[HH:mm:ss.zzz] ") + msg;
    m_logBox->appendPlainText(line);
}

void MainWindow::startSimulation() {
    onSimStart();
}

void MainWindow::onSimStart() {
    if (m_simRunning) return;
    applyUiToConfig();
    m_cfg.save();

    // 新建 CSV 会话
    if (!m_csv.startSession(m_cfg.resolvedCsvDir(), QDateTime::currentDateTime())) {
        statusBar()->showMessage("CSV 文件创建失败", 5000);
        appendLog("[错误] CSV 文件创建失败");
        return;
    }
    m_chartMgr.clear();
    m_simElapsedSec = 0;
    m_simRunning = true;
    appendLog(QString("模拟模式启动 | 节点ID%1~ID%2 周期=%3ms")
        .arg(m_cfg.startNodeId).arg(m_cfg.startNodeId + m_cfg.nodeCount - 1)
        .arg(m_cfg.samplePeriodMs));
    appendLog(QString("CSV 文件: %1").arg(QFileInfo(m_csv.currentFile()).fileName()));

    // 创建定时器，按采样周期触发
    if (!m_simTimer) {
        m_simTimer = new QTimer(this);
        connect(m_simTimer, &QTimer::timeout, this, &MainWindow::onSimTick);
    }
    m_simTimer->start(m_cfg.samplePeriodMs);
    m_startBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);
    statusBar()->showMessage("模拟模式运行中");
}

void MainWindow::onSimStop() {
    if (m_simTimer) m_simTimer->stop();
    m_simRunning = false;
    m_csv.close();
    m_startBtn->setEnabled(true);
    m_stopBtn->setEnabled(false);
    statusBar()->showMessage("模拟模式已停止");
    appendLog("模拟模式已停止，CSV 文件已关闭");
}

void MainWindow::onSimTick() {
    m_simElapsedSec++;
    QVector<Sample> samples;
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 生成正弦波温度数据
    // ID1: 20℃ 基准，振幅 5℃，周期 60 秒
    // ID2: 25℃ 基准，振幅 8℃，周期 90 秒，相位偏移 30 秒
    for (int i = 0; i < m_cfg.nodeCount; ++i) {
        int id = m_cfg.startNodeId + i;
        double baseTemp = (id == 1) ? 20.0 : 25.0;
        double amplitude = (id == 1) ? 5.0 : 8.0;
        double period = (id == 1) ? 60.0 : 90.0;
        double phase = (id == 1) ? 0.0 : 30.0;

        double temp = baseTemp + amplitude * qSin(2.0 * M_PI * (m_simElapsedSec - phase) / period);

        // 添加随机噪声 ±0.3℃
        static std::mt19937 rng(42);
        std::uniform_real_distribution<double> noise(-0.3, 0.3);
        temp += noise(rng);

        Sample s;
        s.timestampMs = now;
        s.nodeId = id;
        s.tempCelsius = temp;
        s.raw = static_cast<quint16>(qRound(temp * 10.0));
        s.online = 1;
        s.alarm = checkAlarm(temp,
            m_cfg.alarmLow.value(id, -10.0),
            m_cfg.alarmHigh.value(id, 60.0));
        samples.append(s);
    }

    // 触发数据处理
    onDataReady(samples);
}
