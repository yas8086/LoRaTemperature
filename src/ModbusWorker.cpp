#include "ModbusWorker.h"
#include <QtSerialBus>   // 包含 QModbusRtuSerialClient/QModbusReply/QModbusDataUnit
#include <QSerialPort>
#include <QDateTime>

ModbusWorker::ModbusWorker(QObject *parent) : QObject(parent) {
}

ModbusWorker::~ModbusWorker() {
    stop();
    if (m_master) m_master->deleteLater();
}

void ModbusWorker::start(const AppConfig &cfg) {
    m_cfg = cfg;
    if (m_master) { m_master->deleteLater(); m_master = nullptr; }
    if (m_timer)  { m_timer->deleteLater(); m_timer = nullptr; }

    m_master = new QModbusRtuSerialClient(this);
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
    // 在子线程动态创建 QTimer，确保事件循环正确运行
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &ModbusWorker::onTimeout);
    m_timer->start(m_cfg.samplePeriodMs);
    onTimeout();   // 立即采集一次
}

void ModbusWorker::stop() {
    if (m_timer) { m_timer->stop(); m_timer->deleteLater(); m_timer = nullptr; }
    m_running = false;
    if (m_master) m_master->disconnectDevice();
    emit statusMessage("已停止");
}

void ModbusWorker::setSamplePeriod(int ms) {
    m_cfg.samplePeriodMs = ms;
    if (m_running && m_timer && m_timer->isActive()) {
        m_timer->start(ms);  // 重启定时器，立即应用新周期
    }
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
        s.alarm       = checkAlarm(s.tempCelsius, m_cfg.alarmLow, m_cfg.alarmHigh);
        samples.append(s);
    }
    emit dataReady(samples);
}
