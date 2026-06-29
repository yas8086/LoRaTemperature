#pragma once
#include <QtGlobal>

// 单次采集数据样本
struct Sample {
    qint64  timestampMs;   // 采集时间戳（epoch 毫秒）
    int     nodeId;        // 节点 ID（1~16）
    quint16 raw;           // 寄存器原始值
    qreal   tempCelsius;   // 解析后温度（℃）
    int     online;        // 1=本次成功读取，0=未读到
    int     alarm;         // 0=正常，1=超上限，-1=超下限
};

// 温度解析：Modbus 大端 quint16 -> qint16 补码 -> /10.0
// 例：raw=0x00BF(191) -> 19.1℃；raw=0xFF60(65376) -> -16.0℃
inline qreal parseTempCelsius(quint16 raw) {
    return static_cast<qint16>(raw) / 10.0;
}

// 报警判定：返回 0 正常 / 1 超上限 / -1 超下限
inline int checkAlarm(qreal temp, qreal low, qreal high) {
    if (temp > high)  return 1;
    if (temp < low)   return -1;
    return 0;
}
