#ifndef RAWWELLDATA_H
#define RAWWELLDATA_H

#include <QString>

/**
 * @brief 一条油井生产日报原始数据。
 *
 * 【标准化后的单位约定】DataNormalizer 会把所有数值统一换算成下面这组标准单位：
 *   productionTime  生产时间     h
 *   dailyLiquid     日产液       t/d
 *   dailyOil        日产油       t/d
 *   dailyWater      日产水       t/d
 *   dailyGas        日产气       m3/d
 *   oilPressure     油压         MPa
 *   casingPressure  套压         MPa
 *   dynamicLevel    动液面       m
 *   dailyInjection  日注水       m3/d
 *
 * 如果原始数据的单位不是上述单位，用 DataNormalizer::setInputUnit() 声明一下即可。
 */
struct RawWellData
{
    QString wellName;                   // 井号
    QString date;                       // 日期
    QString layer;                      // 层位
    double productionTime = 0.0;        // 生产时间
    double dailyLiquid    = 0.0;        // 日产液
    double dailyOil       = 0.0;        // 日产油
    double dailyWater     = 0.0;        // 日产水
    double dailyGas       = 0.0;        // 日产气
    double oilPressure    = 0.0;        // 油压
    double casingPressure = 0.0;        // 套压
    double dynamicLevel   = 0.0;        // 动液面
    double dailyInjection = 0.0;        // 日注水
    bool    isAnomaly      = false;     // 是否异常
    QString anomalyMessage;             // 异常说明（多条用 "; " 分隔）
};

#endif // RAWWELLDATA_H
