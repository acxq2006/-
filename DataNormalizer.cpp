#include "DataNormalizer.h"

#include <QRegularExpression>
#include <QLocale>
#include <QSet>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <limits>

// ============================================================================
// 内部工具函数
// ============================================================================
namespace {

/** 单位名归一化键：全角转半角 -> 小写 -> 去空格/度符号/乘方符号，微符号统一成 u */
QString unitKey(const QString& s)
{
    QString k = s.normalized(QString::NormalizationForm_KC).simplified().toLower();
    k.remove(QLatin1Char(' '));
    k.remove(QChar(0x00B0));                     // °
    k.remove(QChar(0x00B7));                     // ·
    k.remove(QLatin1Char('^'));                  // m^3 -> m3
    k.replace(QChar(0x00B5), QLatin1Char('u'));  // µ
    k.replace(QChar(0x03BC), QLatin1Char('u'));  // μ
    return k;
}

/** 数值转文本，用于拼异常信息 */
QString fmtNum(double v)
{
    if (std::isnan(v))
        return QStringLiteral("NaN");
    return QString::number(v, 'f', 3);
}

/** 合法数值字面量：可选符号 + 整数/小数 + 可选科学计数法指数 */
const QRegularExpression& numberLiteralRe()
{
    static const QRegularExpression re(
        QStringLiteral("^[+-]?(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][+-]?\\d+)?$"));
    return re;
}

/** 数字分组符号（千分位）：只吃夹在数字中间的那些，避免误伤 "1,,2" 之类的畸形串 */
const QRegularExpression& groupSeparatorRe()
{
    static const QRegularExpression re(QStringLiteral("(?<=\\d)[,_ ](?=\\d)"));
    return re;
}

/** 内置缺失值文本（大小写不敏感）；NFKC 归一后比较 */
bool isNullWord(const QString& s)
{
    static const QSet<QString> kWords = {
        QStringLiteral("-"),        QStringLiteral("--"),       QStringLiteral("---"),
        QStringLiteral("/"),        QStringLiteral("\\"),
        QStringLiteral("na"),       QStringLiteral("n/a"),      QStringLiteral("null"),
        QStringLiteral("nan"),      QStringLiteral("none"),     QStringLiteral("nil"),
        QStringLiteral("#n/a"),     QStringLiteral("#value!"),  QStringLiteral("#div/0!"),
        QStringLiteral("无"),        QStringLiteral("空"),        QStringLiteral("缺"),
        QStringLiteral("\u2014")    // —（长破折号，NFKC 不会改写）
    };
    return kWords.contains(s.toLower());
}

/** 按顺序试一串日期格式，命中即返回 */
bool tryDateFormats(const QLocale& c, const QString& s, const QStringList& formats, QDateTime& out)
{
    for (const QString& f : formats) {
        const QDateTime dt = c.toDateTime(s, f);
        if (dt.isValid()) {
            out = dt;
            return true;
        }
    }
    return false;
}

} // namespace

// ============================================================================
// 构造 + 默认规则
// ============================================================================
DataNormalizer::DataNormalizer()
{
    setupDefaults();
}

void DataNormalizer::setupDefaults()
{
    // ---------------------------------------------------------------- 单位表
    // duration：生产时间 -> h
    setUnitRule(QStringLiteral("duration"), QStringLiteral("h"),   1.0);
    setUnitRule(QStringLiteral("duration"), QStringLiteral("d"),   24.0);
    setUnitRule(QStringLiteral("duration"), QStringLiteral("min"), 1.0 / 60.0);
    setUnitRule(QStringLiteral("duration"), QStringLiteral("s"),   1.0 / 3600.0);
    setUnitAlias(QStringLiteral("duration"), QStringLiteral("hour"),   QStringLiteral("h"));
    setUnitAlias(QStringLiteral("duration"), QStringLiteral("hours"),  QStringLiteral("h"));
    setUnitAlias(QStringLiteral("duration"), QStringLiteral("小时"),    QStringLiteral("h"));
    setUnitAlias(QStringLiteral("duration"), QStringLiteral("day"),    QStringLiteral("d"));
    setUnitAlias(QStringLiteral("duration"), QStringLiteral("days"),   QStringLiteral("d"));
    setUnitAlias(QStringLiteral("duration"), QStringLiteral("天"),      QStringLiteral("d"));
    setUnitAlias(QStringLiteral("duration"), QStringLiteral("minute"), QStringLiteral("min"));
    setUnitAlias(QStringLiteral("duration"), QStringLiteral("分钟"),    QStringLiteral("min"));

    // mass_rate：产液/产油/产水 -> t/d
    setUnitRule(QStringLiteral("mass_rate"), QStringLiteral("t/d"),   1.0);
    setUnitRule(QStringLiteral("mass_rate"), QStringLiteral("kg/d"),  0.001);
    setUnitRule(QStringLiteral("mass_rate"), QStringLiteral("t/h"),   24.0);
    setUnitRule(QStringLiteral("mass_rate"), QStringLiteral("t/mon"), 1.0 / 30.0);
    setUnitAlias(QStringLiteral("mass_rate"), QStringLiteral("tpd"),      QStringLiteral("t/d"));
    setUnitAlias(QStringLiteral("mass_rate"), QStringLiteral("吨/天"),     QStringLiteral("t/d"));
    setUnitAlias(QStringLiteral("mass_rate"), QStringLiteral("吨/日"),     QStringLiteral("t/d"));
    setUnitAlias(QStringLiteral("mass_rate"), QStringLiteral("吨天"),      QStringLiteral("t/d"));

    // gas_rate：产气 -> m3/d
    setUnitRule(QStringLiteral("gas_rate"), QStringLiteral("m3/d"),    1.0);
    setUnitRule(QStringLiteral("gas_rate"), QStringLiteral("m3/h"),    24.0);
    setUnitRule(QStringLiteral("gas_rate"), QStringLiteral("万方/d"),   1.0e4);
    setUnitRule(QStringLiteral("gas_rate"), QStringLiteral("104m3/d"), 1.0e4);   // 10^4 m3/d
    setUnitAlias(QStringLiteral("gas_rate"), QStringLiteral("方/天"),      QStringLiteral("m3/d"));
    setUnitAlias(QStringLiteral("gas_rate"), QStringLiteral("方/日"),      QStringLiteral("m3/d"));
    setUnitAlias(QStringLiteral("gas_rate"), QStringLiteral("立方米/天"),   QStringLiteral("m3/d"));
    setUnitAlias(QStringLiteral("gas_rate"), QStringLiteral("万方/天"),     QStringLiteral("万方/d"));
    setUnitAlias(QStringLiteral("gas_rate"), QStringLiteral("万m3/d"),     QStringLiteral("万方/d"));

    // pressure：油压/套压 -> MPa
    setUnitRule(QStringLiteral("pressure"), QStringLiteral("MPa"),     1.0);
    setUnitRule(QStringLiteral("pressure"), QStringLiteral("kPa"),     0.001);
    setUnitRule(QStringLiteral("pressure"), QStringLiteral("Pa"),      1.0e-6);
    setUnitRule(QStringLiteral("pressure"), QStringLiteral("bar"),     0.1);
    setUnitRule(QStringLiteral("pressure"), QStringLiteral("mbar"),    1.0e-4);
    setUnitRule(QStringLiteral("pressure"), QStringLiteral("psi"),     0.00689475729);
    setUnitRule(QStringLiteral("pressure"), QStringLiteral("ksi"),     6.89475729);
    setUnitRule(QStringLiteral("pressure"), QStringLiteral("atm"),     0.101325);
    setUnitRule(QStringLiteral("pressure"), QStringLiteral("kgf/cm2"), 0.0980665);
    setUnitAlias(QStringLiteral("pressure"), QStringLiteral("兆帕"), QStringLiteral("MPa"));
    setUnitAlias(QStringLiteral("pressure"), QStringLiteral("千帕"), QStringLiteral("kPa"));
    setUnitAlias(QStringLiteral("pressure"), QStringLiteral("帕"),   QStringLiteral("Pa"));

    // depth：动液面 -> m
    setUnitRule(QStringLiteral("depth"), QStringLiteral("m"),  1.0);
    setUnitRule(QStringLiteral("depth"), QStringLiteral("km"), 1000.0);
    setUnitRule(QStringLiteral("depth"), QStringLiteral("cm"), 0.01);
    setUnitRule(QStringLiteral("depth"), QStringLiteral("mm"), 0.001);
    setUnitRule(QStringLiteral("depth"), QStringLiteral("ft"), 0.3048);
    setUnitRule(QStringLiteral("depth"), QStringLiteral("in"), 0.0254);
    setUnitAlias(QStringLiteral("depth"), QStringLiteral("米"),   QStringLiteral("m"));
    setUnitAlias(QStringLiteral("depth"), QStringLiteral("千米"), QStringLiteral("km"));
    setUnitAlias(QStringLiteral("depth"), QStringLiteral("公里"), QStringLiteral("km"));
    setUnitAlias(QStringLiteral("depth"), QStringLiteral("英尺"), QStringLiteral("ft"));
    setUnitAlias(QStringLiteral("depth"), QStringLiteral("英寸"), QStringLiteral("in"));

    // volume_rate：日注水 -> m3/d
    setUnitRule(QStringLiteral("volume_rate"), QStringLiteral("m3/d"),   1.0);
    setUnitRule(QStringLiteral("volume_rate"), QStringLiteral("m3/h"),   24.0);
    setUnitRule(QStringLiteral("volume_rate"), QStringLiteral("L/d"),    0.001);
    setUnitRule(QStringLiteral("volume_rate"), QStringLiteral("万方/d"),  1.0e4);
    setUnitRule(QStringLiteral("volume_rate"), QStringLiteral("bbl/d"),  0.158987);
    setUnitAlias(QStringLiteral("volume_rate"), QStringLiteral("方/天"),      QStringLiteral("m3/d"));
    setUnitAlias(QStringLiteral("volume_rate"), QStringLiteral("方/日"),      QStringLiteral("m3/d"));
    setUnitAlias(QStringLiteral("volume_rate"), QStringLiteral("立方米/天"),   QStringLiteral("m3/d"));
    setUnitAlias(QStringLiteral("volume_rate"), QStringLiteral("万方/天"),     QStringLiteral("万方/d"));

    // ------------------------------------------------------------ 缺失值哨兵
    m_missingMarkers << -9999.0 << -9999.9 << -999.25 << -999.0 << -1000.0 << 1.0e30;

    // ---------------------------------------------------------------- 精度
    setPrecision(Field::ProductionTime,  2);
    setPrecision(Field::DailyLiquid,     2);
    setPrecision(Field::DailyOil,        2);
    setPrecision(Field::DailyWater,      2);
    setPrecision(Field::DailyGas,        0);
    setPrecision(Field::OilPressure,     2);
    setPrecision(Field::CasingPressure,  2);
    setPrecision(Field::DynamicLevel,    1);
    setPrecision(Field::DailyInjection,  1);

    // ------------------------------------------------------------ 合理区间
    setRange(Field::ProductionTime,   0.0,    24.0);
    setRange(Field::DailyLiquid,      0.0,  2000.0);
    setRange(Field::DailyOil,         0.0,  2000.0);
    setRange(Field::DailyWater,       0.0,  2000.0);
    setRange(Field::DailyGas,         0.0, 5.0e5);
    setRange(Field::OilPressure,      0.0,    60.0);
    setRange(Field::CasingPressure,   0.0,    60.0);
    setRange(Field::DynamicLevel,     0.0,  5000.0);
    setRange(Field::DailyInjection,   0.0,  5000.0);

    // ------------------------------------------------------------ 日期格式
    // 分成三组：无歧义 / 歧义-日在前 / 歧义-月在前。
    // 只有「日、月都 ≤ 12」的纯数字格式才真的两可（03/05/2026），
    // 这类格式按 setDateOrder() 决定的顺序尝试，另一组用来判定"是否真的歧义"。
    m_dateFormats << QStringLiteral("yyyy-M-d")       << QStringLiteral("yyyy/M/d")
                  << QStringLiteral("yyyy.M.d")       << QStringLiteral("yyyyMMdd")
                  << QStringLiteral("yyyy-M-d H:m")   << QStringLiteral("yyyy-M-d H:m:s")
                  << QStringLiteral("yyyy/M/d H:m:s") << QStringLiteral("yyyy.M.d H:m")
                  << QStringLiteral("yyyy-M-dTh:m:s")
                  << QStringLiteral("yyyy年M月d日")    << QStringLiteral("yyyy年M月d日 H:m:s")
                  << QStringLiteral("yyyy-M-d h:m AP")
                  << QStringLiteral("d-MMM-yyyy")     << QStringLiteral("dd-MMM-yyyy")
                  << QStringLiteral("MMM d, yyyy")    << QStringLiteral("d MMM yyyy");

    m_dateFormatsDayFirst << QStringLiteral("d/M/yyyy")
                          << QStringLiteral("d-M-yyyy")
                          << QStringLiteral("d.M.yyyy");

    m_dateFormatsMonthFirst << QStringLiteral("M/d/yyyy")
                            << QStringLiteral("M-d-yyyy")
                            << QStringLiteral("M.d.yyyy");
}

// ============================================================================
// 字段 <-> 元信息
// ============================================================================
QString DataNormalizer::familyOf(Field f)
{
    switch (f) {
    case Field::ProductionTime: return QStringLiteral("duration");
    case Field::DailyLiquid:
    case Field::DailyOil:
    case Field::DailyWater:     return QStringLiteral("mass_rate");
    case Field::DailyGas:       return QStringLiteral("gas_rate");
    case Field::OilPressure:
    case Field::CasingPressure: return QStringLiteral("pressure");
    case Field::DynamicLevel:   return QStringLiteral("depth");
    case Field::DailyInjection: return QStringLiteral("volume_rate");
    case Field::FieldCount:     break;
    }
    return QString();
}

QString DataNormalizer::canonicalOf(Field f)
{
    switch (f) {
    case Field::ProductionTime: return QStringLiteral("h");
    case Field::DailyLiquid:
    case Field::DailyOil:
    case Field::DailyWater:     return QStringLiteral("t/d");
    case Field::DailyGas:       return QStringLiteral("m3/d");
    case Field::OilPressure:
    case Field::CasingPressure: return QStringLiteral("MPa");
    case Field::DynamicLevel:   return QStringLiteral("m");
    case Field::DailyInjection: return QStringLiteral("m3/d");
    case Field::FieldCount:     break;
    }
    return QString();
}

QString DataNormalizer::nameOf(Field f)
{
    switch (f) {
    case Field::ProductionTime: return QStringLiteral("生产时间");
    case Field::DailyLiquid:    return QStringLiteral("日产液");
    case Field::DailyOil:       return QStringLiteral("日产油");
    case Field::DailyWater:     return QStringLiteral("日产水");
    case Field::DailyGas:       return QStringLiteral("日产气");
    case Field::OilPressure:    return QStringLiteral("油压");
    case Field::CasingPressure: return QStringLiteral("套压");
    case Field::DynamicLevel:   return QStringLiteral("动液面");
    case Field::DailyInjection: return QStringLiteral("日注水");
    case Field::FieldCount:     break;
    }
    return QStringLiteral("未知字段");
}

double& DataNormalizer::fieldRef(RawWellData& d, Field f)
{
    switch (f) {
    case Field::ProductionTime: return d.productionTime;
    case Field::DailyLiquid:    return d.dailyLiquid;
    case Field::DailyOil:       return d.dailyOil;
    case Field::DailyWater:     return d.dailyWater;
    case Field::DailyGas:       return d.dailyGas;
    case Field::OilPressure:    return d.oilPressure;
    case Field::CasingPressure: return d.casingPressure;
    case Field::DynamicLevel:   return d.dynamicLevel;
    case Field::DailyInjection: return d.dailyInjection;
    case Field::FieldCount:     break;
    }
    return d.productionTime;   // 不可达
}

double DataNormalizer::fieldVal(const RawWellData& d, Field f)
{
    switch (f) {
    case Field::ProductionTime: return d.productionTime;
    case Field::DailyLiquid:    return d.dailyLiquid;
    case Field::DailyOil:       return d.dailyOil;
    case Field::DailyWater:     return d.dailyWater;
    case Field::DailyGas:       return d.dailyGas;
    case Field::OilPressure:    return d.oilPressure;
    case Field::CasingPressure: return d.casingPressure;
    case Field::DynamicLevel:   return d.dynamicLevel;
    case Field::DailyInjection: return d.dailyInjection;
    case Field::FieldCount:     break;
    }
    return 0.0;
}

// ============================================================================
// 文本标准化
// ============================================================================
QString DataNormalizer::normalizeWellName(const QString& raw) const
{
    QString s = raw.normalized(QString::NormalizationForm_KC);

    // 各种破折号统一成半角 '-'
    s.replace(QChar(0xFF0D), QLatin1Char('-'));   // －
    s.replace(QChar(0x2013), QLatin1Char('-'));   // –
    s.replace(QChar(0x2014), QLatin1Char('-'));   // —
    s.replace(QChar(0x2212), QLatin1Char('-'));   // −

    // 去掉所有空白（井号里不该有空格）
    s.remove(QRegularExpression(QStringLiteral("\\s+")));

    QHash<QString, QString>::const_iterator it = m_wellAlias.constFind(s);
    if (it != m_wellAlias.constEnd())
        return it.value();

    if (m_wellUpper)
        s = s.toUpper();

    it = m_wellAlias.constFind(s);
    if (it != m_wellAlias.constEnd())
        return it.value();

    return s;
}

QString DataNormalizer::normalizeLayer(const QString& raw) const
{
    QString s = raw.normalized(QString::NormalizationForm_KC).simplified();
    s.remove(QRegularExpression(QStringLiteral("\\s+")));   // "S 2+3" -> "S2+3"

    QHash<QString, QString>::const_iterator it = m_layerAlias.constFind(s);
    if (it != m_layerAlias.constEnd())
        return it.value();

    if (m_layerUpper)
        s = s.toUpper();

    it = m_layerAlias.constFind(s);
    if (it != m_layerAlias.constEnd())
        return it.value();

    return s;
}

// ============================================================================
// 日期标准化
// ============================================================================
bool DataNormalizer::parseDateTime(const QString& raw, QDateTime& out, bool* ambiguous) const
{
    if (ambiguous)
        *ambiguous = false;

    const QString s = raw.normalized(QString::NormalizationForm_KC).simplified();
    if (s.isEmpty())
        return false;

    // ISO 优先（覆盖 2026-09-27 / 2026-09-27T21:48:49 这类）
    QDateTime dt = QDateTime::fromString(s, Qt::ISODate);
    if (dt.isValid()) {
        out = dt;
        return true;
    }

    const QLocale c = QLocale::c();

    // 1) 无歧义格式：年在先，或含月份英文名
    if (tryDateFormats(c, s, m_dateFormats, out))
        return true;

    // 2) 日月歧义格式：两组都试一遍
    //    - 只有偏好的那组能解析 -> 用它的结果，不算歧义（例如 13/05/2026）
    //    - 两组都能解析       -> 真的两可，取偏好组，并把 ambiguous 置 true
    const bool monthFirst = (m_dateOrder == DateOrder::MonthFirst);
    const QStringList& prefer = monthFirst ? m_dateFormatsMonthFirst : m_dateFormatsDayFirst;
    const QStringList& other  = monthFirst ? m_dateFormatsDayFirst : m_dateFormatsMonthFirst;

    QDateTime preferred, alternative;
    const bool okPrefer = tryDateFormats(c, s, prefer, preferred);
    const bool okOther  = tryDateFormats(c, s, other,  alternative);

    if (okPrefer) {
        out = preferred;
        if (ambiguous && okOther)
            *ambiguous = true;
        return true;
    }
    if (okOther) {
        out = alternative;
        return true;
    }
    return false;
}

QString DataNormalizer::normalizeDate(const QString& raw, bool* ok, bool* ambiguous) const
{
    QDateTime dt;
    bool amb = false;
    const bool good = parseDateTime(raw, dt, &amb);
    if (ok)
        *ok = good;
    if (ambiguous)
        *ambiguous = good && amb;
    return good ? dt.toString(m_dateOut) : raw.trimmed();
}

// ============================================================================
// 单位换算
// ============================================================================
bool DataNormalizer::convertUnit(Field f, double v, double& out) const
{
    out = v;

    const QString unit = m_inputUnit.value(int(f)).trimmed();
    if (unit.isEmpty())
        return true;                    // 没声明输入单位 -> 认为已经是标准单位

    const QString fam = familyOf(f);
    QHash<QString, QHash<QString, UnitRule> >::const_iterator fit = m_units.constFind(fam);
    if (fit == m_units.constEnd())
        return false;
    const QHash<QString, UnitRule>& table = fit.value();

    // 单位名先过别名表
    QString key = unitKey(unit);
    QHash<QString, QHash<QString, QString> >::const_iterator afit = m_unitAlias.constFind(fam);
    if (afit != m_unitAlias.constEnd()) {
        QHash<QString, QString>::const_iterator ait = afit.value().constFind(key);
        if (ait != afit.value().constEnd())
            key = ait.value();
    }

    QHash<QString, UnitRule>::const_iterator uit = table.constFind(key);
    if (uit == table.constEnd())
        return false;                   // 未知单位：不猜，交由调用方处理

    if (std::isnan(v)) {
        out = v;
        return true;
    }
    out = v * uit.value().factor + uit.value().offset;
    return true;
}

// ============================================================================
// 缺失值判定
// ============================================================================
bool DataNormalizer::isMissingMarker(double v) const
{
    if (std::isnan(v))
        return true;
    for (double m : m_missingMarkers) {
        const double tol = 1.0e-6 * std::max(1.0, std::fabs(m));
        if (std::fabs(v - m) <= tol)
            return true;
    }
    return false;
}

// ============================================================================
// 单条标准化
// ============================================================================
RawWellData DataNormalizer::normalizeOne(const RawWellData& src, Report* report) const
{
    RawWellData d = src;

    // 异常标记每次重算，保证 normalize 幂等
    d.isAnomaly = false;
    d.anomalyMessage.clear();

    // 统一登记异常：写进记录 + 收进报告
    auto flag = [&](const QString& msg) {
        appendAnomaly(d, msg);
        if (report)
            warn(report, QStringLiteral("[%1] ").arg(
                     d.wellName.isEmpty() ? QStringLiteral("?") : d.wellName) + msg);
    };

    // ---------------- 1. 井号 ----------------
    const QString wn = normalizeWellName(src.wellName);
    if (report && wn != src.wellName.trimmed())
        ++report->wellRenamed;
    d.wellName = wn;
    if (wn.isEmpty())
        flag(QStringLiteral("井号为空"));

    // ---------------- 2. 层位 ----------------
    const QString ly = normalizeLayer(src.layer);
    if (report && ly != src.layer.trimmed())
        ++report->layerRenamed;
    d.layer = ly;
    if (ly.isEmpty())
        flag(QStringLiteral("层位为空"));

    // ---------------- 3. 日期 ----------------
    if (src.date.trimmed().isEmpty()) {
        d.date.clear();
        flag(QStringLiteral("日期为空"));
    } else {
        bool ok = false;
        bool ambiguous = false;
        d.date = normalizeDate(src.date, &ok, &ambiguous);
        if (ok) {
            if (report) ++report->dateParsed;
            if (ambiguous) {
                // 日月都 ≤ 12，两种解释都成立。按选定规则解析是合理的，所以不算异常，
                // 但必须留下痕迹 —— 否则一旦规则选错，整批日期都是错的且无人察觉
                if (report) ++report->dateAmbiguous;
                warn(report, QStringLiteral("[%1] 日期 %2 日月有歧义，按「%3」解释为 %4"
                                            "（要换另一种顺序请调用 setDateOrder）")
                                 .arg(d.wellName.isEmpty() ? QStringLiteral("?") : d.wellName,
                                      src.date,
                                      m_dateOrder == DateOrder::MonthFirst
                                          ? QStringLiteral("月/日") : QStringLiteral("日/月"),
                                      d.date));
            }
        } else {
            if (report) ++report->dateFailed;
            flag(QStringLiteral("日期无法解析: %1").arg(src.date));
        }
    }

    // ---------------- 4. 九个数值字段：缺失 -> 单位 -> 精度 ----------------
    for (int i = 0; i < int(Field::FieldCount); ++i) {
        const Field f = Field(i);
        double v = fieldVal(src, f);

        // 4.0 非法数值：±Inf（NaN 归入 4.1 缺失值一起处理）
        if (std::isinf(v)) {
            if (report) ++report->invalidNumber;
            const QString original = (v > 0.0) ? QStringLiteral("+Inf") : QStringLiteral("-Inf");
            switch (m_missingPolicy) {
            case MissingPolicy::Zero: v = 0.0; break;
            case MissingPolicy::NaN:  v = std::numeric_limits<double>::quiet_NaN(); break;
            case MissingPolicy::Keep: break;
            }
            fieldRef(d, f) = v;
            flag(QStringLiteral("%1：非法数值（%2）").arg(nameOf(f), original));
            continue;
        }

        // 4.1 缺失值
        if (isMissingMarker(v)) {
            if (report) ++report->missingFound;
            const double original = v;
            switch (m_missingPolicy) {
            case MissingPolicy::Zero: v = 0.0; break;
            case MissingPolicy::NaN:  v = std::numeric_limits<double>::quiet_NaN(); break;
            case MissingPolicy::Keep: break;
            }
            fieldRef(d, f) = v;
            flag(QStringLiteral("%1：缺失值（哨兵 %2）").arg(nameOf(f), fmtNum(original)));
            continue;
        }

        // 4.2 单位换算
        const QString inUnit = m_inputUnit.value(i);
        if (!inUnit.isEmpty()) {
            double cv = v;
            if (convertUnit(f, v, cv)) {
                if (cv != v && report)
                    ++report->unitConverted;
                v = cv;
            } else {
                flag(QStringLiteral("%1 的单位无法识别: %2（已按原值保留）")
                         .arg(nameOf(f), inUnit));
            }
        }

        // 4.3 精度
        QHash<int, int>::const_iterator pit = m_precision.constFind(i);
        if (pit != m_precision.constEnd() && pit.value() >= 0) {
            const double p = std::pow(10.0, pit.value());
            v = std::round(v * p) / p;
        }

        // 4.4 归一 -0（-0.0 == 0 为真，但打印出来是 "-0"，下游比对容易出岔子）
        if (v == 0.0)
            v = 0.0;

        fieldRef(d, f) = v;
    }

    // ---------------- 5. 区间校验 ----------------
    for (int i = 0; i < int(Field::FieldCount); ++i) {
        const Field f = Field(i);
        const double v = fieldVal(d, f);
        if (std::isnan(v))
            continue;
        QHash<int, Range>::const_iterator rit = m_ranges.constFind(i);
        if (rit == m_ranges.constEnd())
            continue;
        const Range& rg = rit.value();
        if (v < rg.min || v > rg.max) {
            if (report) ++report->outOfRange;
            flag(QStringLiteral("%1 超出合理区间 [%2, %3]，实际 %4")
                     .arg(nameOf(f), fmtNum(rg.min), fmtNum(rg.max), fmtNum(v)));
        }
    }

    // ---------------- 6. 跨字段一致性 ----------------
    if (m_checkBalance) {
        if (d.dailyLiquid > 0.0) {
            const double sum = d.dailyOil + d.dailyWater;
            const double rel = std::fabs(sum - d.dailyLiquid) / d.dailyLiquid;
            if (rel > m_balanceTol) {
                flag(QStringLiteral("液量不平衡：油 %1 + 水 %2 = %3，与日产液 %4 相差 %5%")
                         .arg(fmtNum(d.dailyOil), fmtNum(d.dailyWater), fmtNum(sum),
                              fmtNum(d.dailyLiquid), fmtNum(rel * 100.0)));
            }
            if (d.dailyWater > d.dailyLiquid) {
                flag(QStringLiteral("日产水 %1 大于日产液 %2，含水率超过 100%")
                         .arg(fmtNum(d.dailyWater), fmtNum(d.dailyLiquid)));
            }
        }
        if (d.productionTime <= 0.0 && d.dailyLiquid > 0.0) {
            flag(QStringLiteral("生产时间为 0 但日产液为 %1，数据矛盾")
                     .arg(fmtNum(d.dailyLiquid)));
        }
        if (d.dailyLiquid > 0.0 && d.dailyOil <= 0.0 && d.dailyWater <= 0.0) {
            flag(QStringLiteral("日产液为 %1，但日产油与日产水均为 0，数据矛盾")
                     .arg(fmtNum(d.dailyLiquid)));
        }
    }

    return d;
}

// ============================================================================
// 批量标准化
// ============================================================================
QVector<RawWellData> DataNormalizer::normalize(const QVector<RawWellData>& data, Report* report) const
{
    Report local;
    QVector<RawWellData> out;
    out.reserve(data.size());

    for (const RawWellData& src : data) {
        const RawWellData d = normalizeOne(src, &local);
        if (d.isAnomaly)
            ++local.anomalyRows;
        out.append(d);
    }

    local.total = data.size();
    if (report)
        *report = local;

    return out;
}

// ============================================================================
// 数值文本解析 / 格式化
// ============================================================================
bool DataNormalizer::isFiniteNumber(double v)
{
    return std::isfinite(v);
}

bool DataNormalizer::parseNumber(const QString& text, double* out,
                                 const NumberTextRule& rule, QString* reason)
{
    if (out)
        *out = 0.0;

    // 统一失败出口，顺便把原因写出去
    auto fail = [&](const QString& why) {
        if (reason)
            *reason = why;
        return false;
    };

    QString s = text.trimmed();
    if (s.isEmpty())
        return fail(QStringLiteral("空文本"));

    // 1) 兼容字符归一：全角数字 / 全角符号 -> 半角（１２３ -> 123，－ -> -，％ -> %）
    if (rule.fullWidthDigits)
        s = s.normalized(QString::NormalizationForm_KC);
    s = s.simplified();

    // 2) 缺失值文本：- / -- / NA / null / #N/A / 无 ...
    if (isNullWord(s))
        return fail(QStringLiteral("缺失值标记「%1」").arg(s));

    // 3) 会计式负数：(123.5) -> -123.5
    bool negativeParen = false;
    if (rule.accountingNegative && s.size() >= 3
        && s.startsWith(QLatin1Char('(')) && s.endsWith(QLatin1Char(')'))) {
        negativeParen = true;
        s = s.mid(1, s.size() - 2).trimmed();
        if (s.isEmpty())
            return fail(QStringLiteral("括号内为空"));
    }

    // 4) 百分号：只去符号，不做 /100 —— 85% 解析成 85 还是 0.85 有语义歧义，
    //    默认关闭，需要时由调用方显式打开并自行除以 100
    if (s.endsWith(QLatin1Char('%'))) {
        if (!rule.percentSign)
            return fail(QStringLiteral("含百分号「%1」").arg(text.trimmed()));
        s.chop(1);
        s = s.trimmed();
    }

    // 5) 中文量级词后缀：1.2万 -> 12000
    double scale = 1.0;
    if (rule.chineseMagnitude) {
        const QString suffix[3] = { QStringLiteral("亿"), QStringLiteral("万"), QStringLiteral("千") };
        const double  factor[3] = { 1.0e8, 1.0e4, 1.0e3 };
        for (int i = 0; i < 3; ++i) {
            if (s.endsWith(suffix[i])) {
                scale = factor[i];
                s.chop(suffix[i].size());
                s = s.trimmed();
                break;
            }
        }
    }

    // 6) 千分位：逗号 / 空格 / 下划线（只吃夹在数字之间的，避免 "1,,2" 变合法）
    if (rule.thousandsSeparator)
        s.replace(groupSeparatorRe(), QString());

    // 7) 字面量校验：正则把关，免得 QLocale 把 "inf"、"0x10" 这类也当数字收下
    if (!numberLiteralRe().match(s).hasMatch())
        return fail(QStringLiteral("不是合法数值「%1」").arg(text.trimmed()));

    if (!rule.scientific
        && (s.contains(QLatin1Char('e')) || s.contains(QLatin1Char('E'))))
        return fail(QStringLiteral("不允许科学计数法「%1」").arg(text.trimmed()));

    // 8) 取值
    bool ok = false;
    double v = QLocale::c().toDouble(s, &ok);
    if (!ok)
        return fail(QStringLiteral("超出 double 可表示范围「%1」").arg(text.trimmed()));

    v *= scale;
    if (negativeParen)
        v = -v;

    if (std::isnan(v))
        return fail(QStringLiteral("结果不是数值「%1」").arg(text.trimmed()));
    if (std::isinf(v))
        return fail(QStringLiteral("数值溢出「%1」").arg(text.trimmed()));

    if (out)
        *out = (v == 0.0) ? 0.0 : v;      // 归一 -0
    if (reason)
        reason->clear();
    return true;
}

QString DataNormalizer::formatNumber(double value, int decimals, bool trimTrailingZeros)
{
    if (std::isnan(value))
        return QStringLiteral("NaN");
    if (std::isinf(value))
        return value > 0.0 ? QStringLiteral("+Inf") : QStringLiteral("-Inf");

    if (value == 0.0)
        value = 0.0;                      // 归一 -0
    if (decimals < 0)
        decimals = 0;

    QString s = QString::number(value, 'f', decimals);

    if (trimTrailingZeros && s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0')))
            s.chop(1);
        if (s.endsWith(QLatin1Char('.')))
            s.chop(1);
    }
    return s;
}

// ============================================================================
// 配置接口
// ============================================================================
void DataNormalizer::setMissingPolicy(MissingPolicy p) { m_missingPolicy = p; }

void DataNormalizer::setMissingMarkers(const QVector<double>& markers) { m_missingMarkers = markers; }

void DataNormalizer::setPrecision(Field f, int decimals) { m_precision.insert(int(f), decimals); }

void DataNormalizer::setInputUnit(Field f, const QString& unit) { m_inputUnit.insert(int(f), unit.trimmed()); }

void DataNormalizer::setUnitRule(const QString& family, const QString& unit, double factor, double offset)
{
    UnitRule r;
    r.factor = factor;
    r.offset = offset;
    m_units[family].insert(unitKey(unit), r);
}

void DataNormalizer::setUnitAlias(const QString& family, const QString& alias, const QString& canonical)
{
    m_unitAlias[family].insert(unitKey(alias), unitKey(canonical));
}

void DataNormalizer::setRange(Field f, double min, double max)
{
    Range r;
    r.min = min;
    r.max = max;
    m_ranges.insert(int(f), r);
}

void DataNormalizer::setWellAlias(const QHash<QString, QString>& map)
{
    for (QHash<QString, QString>::const_iterator it = map.constBegin(); it != map.constEnd(); ++it)
        m_wellAlias.insert(normalizeWellName(it.key()), normalizeWellName(it.value()));
}

void DataNormalizer::setLayerAlias(const QHash<QString, QString>& map)
{
    for (QHash<QString, QString>::const_iterator it = map.constBegin(); it != map.constEnd(); ++it)
        m_layerAlias.insert(normalizeLayer(it.key()), normalizeLayer(it.value()));
}

void DataNormalizer::setWellNameUpper(bool on)  { m_wellUpper  = on; }
void DataNormalizer::setLayerNameUpper(bool on) { m_layerUpper = on; }
void DataNormalizer::setBalanceTolerance(double relative) { m_balanceTol = relative; }
void DataNormalizer::setCheckBalance(bool on)   { m_checkBalance = on; }
void DataNormalizer::setDateOrder(DateOrder order) { m_dateOrder = order; }
void DataNormalizer::setExtraDateFormats(const QStringList& formats) { m_dateFormats << formats; }
void DataNormalizer::setOutputDateFormat(const QString& format) { m_dateOut = format; }

// ============================================================================
// 辅助
// ============================================================================
void DataNormalizer::appendAnomaly(RawWellData& d, const QString& msg)
{
    if (msg.isEmpty())
        return;
    d.isAnomaly = true;
    if (d.anomalyMessage.isEmpty())
        d.anomalyMessage = msg;
    else
        d.anomalyMessage += QStringLiteral("; ") + msg;
}

void DataNormalizer::warn(Report* r, const QString& msg) const
{
    if (!r || msg.isEmpty())
        return;
    if (r->warnings.size() < 100)      // 只留前 100 条，防止脏数据刷爆内存
        r->warnings.append(msg);
}

QString DataNormalizer::Report::toString() const
{
    QString s;
    QTextStream ts(&s);
    ts << QStringLiteral("共 ") << total << QStringLiteral(" 条")
       << QStringLiteral(" | 井号规范化 ") << wellRenamed
       << QStringLiteral(" | 层位规范化 ") << layerRenamed
       << QStringLiteral(" | 日期 成功/失败/歧义 ") << dateParsed << '/' << dateFailed
       << '/' << dateAmbiguous
       << QStringLiteral(" | 单位换算 ") << unitConverted
       << QStringLiteral(" | 缺失值 ") << missingFound
       << QStringLiteral(" | 非法数值 ") << invalidNumber
       << QStringLiteral(" | 越界 ") << outOfRange
       << QStringLiteral(" | 异常记录 ") << anomalyRows;
    if (!warnings.isEmpty()) {
        ts << QStringLiteral("\n告警 ") << warnings.size() << QStringLiteral(" 条:");
        for (const QString& w : warnings)
            ts << QStringLiteral("\n  - ") << w;
    }
    ts.flush();
    return s;
}
