#pragma once
#include "Sample.h"
#include "AppConfig.h"
#include <QObject>
#include <QTimer>
#include <QVector>

class QModbusRtuSerialClient;
class QModbusReply;

class ModbusWorker : public QObject {
    Q_OBJECT
public:
    explicit ModbusWorker(QObject *parent = nullptr);
    ~ModbusWorker();
public slots:
    void start(const AppConfig &cfg);
    void stop();
    void setSamplePeriod(int ms);   // 运行时更新采集周期
signals:
    void dataReady(QVector<Sample> samples);
    void error(const QString &msg);
    void statusMessage(const QString &msg);
private slots:
    void onTimeout();
    void onReplyFinished();
private:
    QModbusRtuSerialClient *m_master = nullptr;
    QTimer  m_timer;
    AppConfig m_cfg;
    int   m_failCount = 0;
    bool  m_running = false;
};
