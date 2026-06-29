#include "ChartManager.h"
#include <QDateTime>

ChartManager::ChartManager() {
    m_chart = new QChart();
    m_chart->setTitle("实时温度曲线");
    m_chart->legend()->setVisible(true);

    m_axisX = new QDateTimeAxis();
    m_axisX->setFormat("HH:mm:ss");
    m_axisX->setTitleText("时间");
    m_chart->addAxis(m_axisX, Qt::AlignBottom);

    m_axisY = new QValueAxis();
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
        auto *s = new QLineSeries();
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
