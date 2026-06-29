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
#include <QFileInfo>
#include <QFrame>
#include <QMetaType>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    // 跨线程信号槽需要注册自定义类型
    qRegisterMetaType<QVector<Sample>>("QVector<Sample>");
    qRegisterMetaType<AppConfig>("AppConfig");

    setWindowTitle("LoRa 温度监测");
    resize(1100, 700);
    buildUi();
    loadConfig();
    applyConfigToUi();
    rebuildCards();
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
    auto *chartView = new QChartView(m_chartMgr.chart());
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
