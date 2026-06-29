#pragma once
#include "AppConfig.h"
#include "CsvWriter.h"
#include "ChartManager.h"
#include <QMainWindow>
#include <QThread>
#include <QHash>
#include <QColor>
class ModbusWorker;
class QComboBox;
class QSpinBox;
class QHBoxLayout;
class QGridLayout;
class QTableWidget;
class QLabel;
class QPushButton;
class QCheckBox;
class QTimer;
class QPlainTextEdit;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    void startSimulation();
private slots:
    void onStart();
    void onStop();
    void onDataReady(QVector<Sample> samples);
    void onError(const QString &msg);
    void onStatus(const QString &msg);
    void onChooseCsvDir();
    void onIdToggled(bool checked);
    void onSimTick();
    void onSimStart();
    void onSimStop();
private:
    void buildUi();
    void loadConfig();
    void applyConfigToUi();
    void applyUiToConfig();
    void rebuildCards();
    void rebuildIdSelectors();
    void appendLog(const QString &msg);

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

    // 温度卡片（网格布局，可多显示几个）
    QWidget       *m_cardContainer;
    QGridLayout   *m_cardLayout;
    QHash<int, QLabel*> m_cardLabels;

    // ID 选择器（多选按钮 + 颜色色块）
    QWidget             *m_idSelectorPanel;
    QGridLayout         *m_idSelectorLayout;
    QHash<int, QCheckBox*> m_idToggles;
    static constexpr int kMaxSelectableIds = 8;
    static const QColor kIdColors[];

    ChartManager   m_chartMgr;
    QTableWidget  *m_table;
    QPlainTextEdit *m_logBox = nullptr;

    AppConfig  m_cfg;
    CsvWriter  m_csv;
    ModbusWorker *m_worker = nullptr;
    QThread   *m_thread = nullptr;

    // 模拟模式
    QTimer    *m_simTimer = nullptr;
    int        m_simElapsedSec = 0;
    bool       m_simRunning = false;
};
