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
