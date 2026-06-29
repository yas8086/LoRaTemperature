#include "AppConfig.h"
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QCoreApplication>

void AppConfig::ensureDefaults() {
    if (csvDir.isEmpty()) {
        // 默认使用当前工作目录下的 data 文件夹（项目根目录/data）
        csvDir = QDir::currentPath() + "/data";
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
