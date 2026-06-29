#pragma once
#include "Sample.h"
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QHash>
#include <QDateTimeAxis>
#include <QValueAxis>

class ChartManager {
public:
    ChartManager();
    QChart* chart() { return m_chart; }
    void setupNodes(int startId, int count);
    void append(const QVector<Sample> &samples);
    void clear();
private:
    QChart *m_chart;
    QDateTimeAxis *m_axisX;
    QValueAxis    *m_axisY;
    QHash<int, QLineSeries*> m_series;  // key=nodeId
    int m_startId = 1;
    int m_count   = 0;
    static constexpr int kMaxPointsPerSeries = 900; // 30min @ 2s
};
