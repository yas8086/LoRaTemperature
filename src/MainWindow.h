#pragma once
#include "AppConfig.h"
#include "CsvWriter.h"
#include "ChartManager.h"
#include <QMainWindow>
#include <QThread>
#include <QHash>
class ModbusWorker;
class QComboBox;
class QSpinBox;
class QGridLayout;
class QTableWidget;
class QLabel;
class QPushButton;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
private slots:
    void onStart();
    void onStop();
    void onDataReady(QVector<Sample> samples);
    void onError(const QString &msg);
    void onStatus(const QString &msg);
    void onChooseCsvDir();
private:
    void buildUi();
    void loadConfig();
    void applyConfigToUi();
    void applyUiToConfig();
    void rebuildCards();

    // 配置面板控件
    QComboBox *m_portCombo;
    QComboBox *m_baudCombo;
    QSpinBox  *m_slaveSpin;
    QSpinBox  *m_startIdSpin;
    QSpinBox  *m_nodeCountSpin;
    QSpinBox  *m_periodSpin;
    QLabel    *m_csvDirLabel;
    QPushButton *m_csvBtn;
    QPushButton *m_startBtn;
    QPushButton *m_stopBtn;

    // 主区域
    QWidget       *m_cardContainer;
    QGridLayout   *m_cardLayout;
    QHash<int, QLabel*> m_cardLabels;   // nodeId -> 大字号温度标签

    ChartManager   m_chartMgr;
    QTableWidget  *m_table;

    AppConfig  m_cfg;
    CsvWriter  m_csv;
    ModbusWorker *m_worker = nullptr;
    QThread   *m_thread = nullptr;
};
