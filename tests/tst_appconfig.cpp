#include <QtTest/QtTest>
#include <QSettings>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
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
    cfg.alarmLow  = -5.0;
    cfg.alarmHigh = 55.0;
    cfg.save();

    AppConfig cfg2;
    cfg2.load();
    QCOMPARE(cfg2.alarmLow,  -5.0);
    QCOMPARE(cfg2.alarmHigh, 55.0);
    QCOMPARE(cfg2.tempRegAddr, quint16(0x76C1));
    QCOMPARE(cfg2.samplePeriodMs, 2000);
}

QTEST_MAIN(TestAppConfig)
#include "tst_appconfig.moc"
