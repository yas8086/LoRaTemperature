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
    QVERIFY(f.open(QIODevice::ReadOnly));
    QByteArray raw = f.readAll();
    // 检查 UTF-8 BOM
    QCOMPARE(raw.left(3), QByteArray("\xEF\xBB\xBF", 3));
    QString content = QString::fromUtf8(raw);
    QVERIFY(content.contains("timestamp,node_id,temp_celsius,raw,online,alarm"));
    QVERIFY(content.contains("2026-06-29 14:30:01.123,1,19.1,191,1,0"));
    QVERIFY(content.contains("2026-06-29 14:30:01.123,2,25.3,253,1,0"));
}

QTEST_MAIN(TestCsvWriter)
#include "tst_csvwriter.moc"
