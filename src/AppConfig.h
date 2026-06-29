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
