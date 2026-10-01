#ifndef DATANORMALIZER_H
#define DATANORMALIZER_H

#include "RawWellData.h"

#include <QVector>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QDateTime>

/**
 * 数值文本解析规则：把 Excel / CSV 里读到的脏文本转成 double。
 *
 * 定义在类外是有意为之 —— 类内声明的默认实参不能去构造「同一个类中定义、
 * 且带默认成员初始化器」的嵌套类型（GCC 会直接报
 * "default member initializer required before the end of its enclosing class"）。
 * 类内用 using 保留了 DataNormalizer::NumberTextRule 这个名字，外部接口不变。
 */
struct NumberTextRule
{
    bool thousandsSeparator = true;   // "1,234.5" / "1 234.5" -> 1234.5
    bool fullWidthDigits    = true;   // "１２３４"        -> 1234
    bool accountingNegative = true;   // "(123.5)"        -> -123.5
    bool scientific         = true;   // "1.5e3"          -> 1500
    bool chineseMagnitude   = true;   // "1.2万" / "3千"   -> 12000 / 3000
    bool percentSign        = false;  // "85%" -> 85（仅去符号，不做 /100；默认关）
};

/**
 * @brief DataNormalizer —— 油井生产数据标准化器
 *
 * 入参 / 出参都是 QVector<RawWellData>（返回新副本，不改动原数据），负责四件事：
 *
 *   1) 文本标准化：井号（去空格/全角转半角/连字符统一/统一大写）、层位（同上）、
 *      日期（各种输入格式 -> 统一输出 "yyyy-MM-dd"，日月歧义可配）
 *   2) 单位标准化：声明各字段的输入单位后自动换算到标准单位（psi->MPa、ft->m ...）
 *   3) 数值标准化：缺失哨兵值（-9999 / -999.25 / NaN）按策略处理、±Inf 拦截、
 *      统一小数位、消除 -0；并提供 parseNumber() / formatNumber() 两个静态工具，
 *      供读入层（Excel / CSV 文本）与输出层复用
 *   4) 合理性校验：越界、负值、油气水平衡等 -> 写入 isAnomaly / anomalyMessage
 *
 * 标准单位：生产时间 h，产液/产油/产水 t/d，产气 m3/d，油压/套压 MPa，
 *          动液面 m，注水 m3/d
 *
 * 典型用法：
 * @code
 *   DataNormalizer n;
 *   n.setInputUnit(DataNormalizer::Field::OilPressure, "psi");   // 这批数据的油压单位是 psi
 *   n.setDateOrder(DataNormalizer::DateOrder::MonthFirst);       // 03/05/2026 按美式月/日解析
 *   DataNormalizer::Report rep;
 *   QVector<RawWellData> clean = n.normalize(rawData, &rep);
 *
 *   // 读入层：把 Excel 里的脏文本转成数值（替代裸 toDouble()）
 *   double v = 0.0;
 *   if (!DataNormalizer::parseNumber(cellText, &v))
 *       qWarning() << "解析失败:" << cellText;
 * @endcode
 *
 * 无状态：规则在调用前装配完毕，normalize() 只读，同一实例可在多线程间共享。
 * 只依赖 Qt Core，console / widgets / service 工程都能直接用。
 */
class DataNormalizer
{
public:
    /** 参与标准化的数值字段 */
    enum class Field {
        ProductionTime,
        DailyLiquid,
        DailyOil,
        DailyWater,
        DailyGas,
        OilPressure,
        CasingPressure,
        DynamicLevel,
        DailyInjection,
        FieldCount
    };

    /** 缺失值处理策略 */
    enum class MissingPolicy {
        Zero,   // 置 0，并在 anomalyMessage 里记一笔（默认）
        NaN,    // 置 NaN，交由下游判断
        Keep    // 原样保留
    };

    /**
     * 日月歧义日期的解析顺序。
     *
     * 只有「日、月都 ≤ 12」时才真的歧义 —— "05/13/2026" 和 "13/05/2026"
     * 无论怎么排都能解析对，所以这个问题很隐蔽，容易上线后才炸。
     */
    enum class DateOrder {
        DayFirst,     // 03/05/2026 -> 2026-05-03（日在前，欧洲习惯）**默认，保持向后兼容**
        MonthFirst    // 03/05/2026 -> 2026-03-05（月在前，美国 / Excel 常见导出格式）
    };

    /** 数值文本解析规则：把 Excel / CSV 里读到的脏文本转成 double */
    using NumberTextRule = ::NumberTextRule;

    /** 一次 normalize 的诊断报告 */
    struct Report
    {
        int         total        = 0;   // 处理记录数
        int         wellRenamed  = 0;   // 井号被规范化的条数
        int         layerRenamed = 0;   // 层位被规范化的条数
        int         dateParsed   = 0;   // 日期解析成功条数
        int         dateFailed   = 0;   // 日期解析失败条数
        int         dateAmbiguous= 0;   // 日月有歧义、按当前 DateOrder 解释的条数
        int         unitConverted= 0;   // 发生单位换算的字段数
        int         missingFound = 0;   // 命中缺失哨兵的字段数
        int         invalidNumber= 0;   // 非法数值（±Inf）字段数
        int         outOfRange   = 0;   // 越界异常数
        int         anomalyRows  = 0;   // 最终被判异常的记录数
        QStringList warnings;           // 明细告警（最多 100 条）

        QString toString() const;
    };

    DataNormalizer();

    // ---------------- 主入口 ----------------

    QVector<RawWellData> normalize(const QVector<RawWellData>& data, Report* report = nullptr) const;
    RawWellData          normalizeOne(const RawWellData& src, Report* report = nullptr) const;

    // ---------------- 配置（不改动核心逻辑即可扩展） ----------------

    void setMissingPolicy(MissingPolicy p);                       // 缺失值策略，默认 Zero
    void setMissingMarkers(const QVector<double>& markers);       // 数值哨兵，默认 -9999 / -999.25 / ...
    void setPrecision(Field f, int decimals);                     // 某字段保留小数位
    void setInputUnit(Field f, const QString& unit);              // 声明某字段的输入单位
    void setUnitRule(const QString& family, const QString& unit,  // 自定义单位换算规则
                     double factor, double offset = 0.0);
    void setUnitAlias(const QString& family, const QString& alias, const QString& canonical);
    void setRange(Field f, double min, double max);               // 合理区间，越界记异常
    void setWellAlias(const QHash<QString, QString>& map);        // 井号别名（旧井号 -> 新井号）
    void setLayerAlias(const QHash<QString, QString>& map);       // 层位别名（葡一组 -> PI）
    void setWellNameUpper(bool on);                               // 井号统一大写，默认开
    void setLayerNameUpper(bool on);                              // 层位统一大写，默认开
    void setBalanceTolerance(double relative);                    // 油+水 vs 液量 允许相对误差，默认 0.05
    void setCheckBalance(bool on);                                // 是否做油气水平衡校验，默认开
    void setDateOrder(DateOrder order);                           // 歧义日期解析顺序，默认 DayFirst
    void setExtraDateFormats(const QStringList& formats);         // 追加自定义日期格式
    void setOutputDateFormat(const QString& format);              // 输出日期格式，默认 yyyy-MM-dd

    // ---------------- 单项工具（便于复用与单测） ----------------

    QString normalizeWellName(const QString& raw) const;
    QString normalizeLayer(const QString& raw) const;
    /**
     * 日期文本 -> 目标格式串；解析失败时原样返回，并通过 ok 告知。
     * @param ambiguous 若该日期「日、月都 ≤ 12」故存在歧义，置 true（可传 nullptr）
     */
    QString normalizeDate(const QString& raw, bool* ok = nullptr, bool* ambiguous = nullptr) const;

    /**
     * 脏数值文本 -> double。可识别：全角数字、千分位（逗号/空格/下划线）、
     * 会计式负数 (123)、科学计数法、中文量级词（万/千/亿），
     * 以及 - / -- / NA / N/A / null / NaN / #N/A / 无 / 空 等缺失标记。
     * @param text   待解析文本
     * @param out    解析结果（失败时置 0）
     * @param rule   解析规则
     * @param reason 失败原因（可选）
     * @return 成功 true；失败 false
     */
    static bool parseNumber(const QString& text, double* out,
                            const NumberTextRule& rule = NumberTextRule(),
                            QString* reason = nullptr);

    /**
     * 数值 -> 统一文本。NaN / ±Inf 有固定写法，-0 归一为 0。
     * @param value              待格式化数值
     * @param decimals           小数位（<0 视为 0）
     * @param trimTrailingZeros  是否去掉尾部多余的 0 与小数点（"10.00" -> "10"）
     */
    static QString formatNumber(double value, int decimals = 2,
                                bool trimTrailingZeros = false);

    /** 是否为有限数值（非 NaN 且非 ±Inf） */
    static bool isFiniteNumber(double v);

    /** 把异常说明追加到记录上，自动置 isAnomaly 并用 "; " 分隔 */
    static void appendAnomaly(RawWellData& d, const QString& msg);

private:
    struct UnitRule { double factor = 1.0; double offset = 0.0; };
    struct Range    { double min = -1e308; double max = 1e308; };

    static QString familyOf(Field f);          // 所属量纲族
    static QString canonicalOf(Field f);       // 标准单位
    static QString nameOf(Field f);            // 中文名，写异常信息用
    static double& fieldRef(RawWellData& d, Field f);
    static double  fieldVal(const RawWellData& d, Field f);

    bool  convertUnit(Field f, double v, double& out) const;
    /** 依次尝试：ISO -> 无歧义格式 -> 按 m_dateOrder 排定的歧义格式 */
    bool  parseDateTime(const QString& raw, QDateTime& out, bool* ambiguous = nullptr) const;
    bool  isMissingMarker(double v) const;
    void  warn(Report* r, const QString& msg) const;
    void  setupDefaults();

    MissingPolicy            m_missingPolicy = MissingPolicy::Zero;
    QVector<double>          m_missingMarkers;
    QHash<int, int>          m_precision;                  // Field -> 小数位
    QHash<int, QString>      m_inputUnit;                  // Field -> 输入单位（空 = 已是标准单位）
    QHash<QString, QHash<QString, UnitRule> > m_units;     // family -> unitKey -> rule
    QHash<QString, QHash<QString, QString> >  m_unitAlias; // family -> aliasKey -> canonicalKey
    QHash<int, Range>        m_ranges;                     // Field -> 合理区间
    QHash<QString, QString>  m_wellAlias;                  // 井号别名
    QHash<QString, QString>  m_layerAlias;                 // 层位别名
    QStringList              m_dateFormats;                // 无歧义格式（年在先 / 含月份英文名）
    QStringList              m_dateFormatsDayFirst;        // 歧义格式：日在前
    QStringList              m_dateFormatsMonthFirst;      // 歧义格式：月在前
    QString                  m_dateOut = QStringLiteral("yyyy-MM-dd");
    DateOrder                m_dateOrder = DateOrder::DayFirst;
    double                   m_balanceTol = 0.05;
    bool                     m_checkBalance = true;
    bool                     m_wellUpper  = true;
    bool                     m_layerUpper = true;
};

#endif // DATANORMALIZER_H
