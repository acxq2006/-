#include "widget.h"
#include "DataNormalizer.h"

#include <QDebug>
#include <QFont>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>

#include <limits>

// ---------------------------------------------------------------------------
// 一个最小的验证界面：点按钮 -> 造 3 条脏数据 -> 跑 normalize -> 把结果摊开显示
// 只依赖 DataNormalizer / RawWellData，界面代码可以随时删掉，不影响模块本身
// ---------------------------------------------------------------------------

Widget::Widget(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("DataNormalizer 测试 — 油井生产数据标准化"));
    resize(900, 660);

    auto *hint = new QLabel(
        QStringLiteral("输入单位：油压/套压 = psi，动液面 = ft　→　输出统一为 MPa / m / t/d\n"
                       "日期「03/05/2026」日月有歧义，默认按 日/月 解释；调用 setDateOrder 可切换"),
        this);

    auto *btn = new QPushButton(QStringLiteral("▶  跑一遍标准化（4 条脏数据）"), this);
    btn->setMinimumHeight(34);

    auto *btnNum = new QPushButton(QStringLiteral("▶  跑一遍数值文本解析（parseNumber / formatNumber）"), this);
    btnNum->setMinimumHeight(34);

    m_output = new QPlainTextEdit(this);
    m_output->setReadOnly(true);
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);   // 关掉自动换行，方便看对齐

    QFont mono = m_output->font();
    mono.setFamily(QStringLiteral("Consolas"));
    mono.setPointSize(10);
    m_output->setFont(mono);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addWidget(btn);
    layout->addWidget(btnNum);
    layout->addWidget(m_output);

    connect(btn,    &QPushButton::clicked, this, &Widget::runNormalize);
    connect(btnNum, &QPushButton::clicked, this, &Widget::runNumberParsing);

    runNormalize();   // 打开窗口就先跑一次，省得你还得点
}

Widget::~Widget() = default;

void Widget::runNormalize()
{
    // ================= 1. 造 4 条"脏数据"（模拟从 Excel / 数据库读进来的样子） =================
    QVector<RawWellData> raw;

    {   // ① 格式脏 + 英制单位：这是最常见的情况
        RawWellData d;
        d.wellName       = QStringLiteral(" x1-2-3 ");       // 前后空格 + 小写
        d.date           = QStringLiteral("2026/3/5");      // 斜杠日期（年在先，无歧义）
        d.layer          = QStringLiteral(" s2+3 ");         // 前后空格 + 小写
        d.productionTime = 24.0;
        d.dailyLiquid    = 30.0;
        d.dailyOil       = 20.0;
        d.dailyWater     = 10.0;
        d.dailyGas       = 1200.0;
        d.oilPressure    = 1500.0;                          // psi -> 约 10.34 MPa
        d.casingPressure = 1200.0;                          // psi -> 约 8.27 MPa
        d.dynamicLevel   = 3200.0;                          // ft  -> 约 975.36 m
        raw << d;
    }

    {   // ② 全角字符 + 非法日期 + 缺失哨兵值
        RawWellData d;
        d.wellName       = QStringLiteral("Ｚ１０６－Ｘ１");    // 全角字符 + 全角破折号
        d.date           = QStringLiteral("2026年13月45日");   // 根本没有这个日期
        d.layer          = QStringLiteral("p1");
        d.productionTime = 24.0;
        d.dailyLiquid    = -9999.0;                          // 哨兵值：缺失
        d.dailyOil       = -9999.0;
        d.dailyWater     = -9999.0;
        d.dailyGas       = -9999.0;
        raw << d;
    }

    {   // ③ 逻辑异常：日产油 + 日产水 对不上 日产液
        RawWellData d;
        d.wellName       = QStringLiteral("X7-9");
        d.date           = QStringLiteral("20260305");       // 紧凑日期
        d.layer          = QStringLiteral("P1");
        d.productionTime = 24.0;
        d.dailyLiquid    = 30.0;
        d.dailyOil       = 5.0;
        d.dailyWater     = 40.0;                             // 5 + 40 = 45，与 30 差太远
        d.dailyGas       = 800.0;
        raw << d;
    }

    {   // ④ 日月歧义："03/05/2026" 是 3 月 5 日还是 5 月 3 日？日、月都 ≤ 12，真的两可
        RawWellData d;
        d.wellName       = QStringLiteral("Y-1");
        d.date           = QStringLiteral("03/05/2026");
        d.layer          = QStringLiteral("P2");
        d.productionTime = 24.0;
        d.dailyLiquid    = 12.0;
        d.dailyOil       = 8.0;
        d.dailyWater     = 4.0;
        d.dailyGas       = 300.0;
        raw << d;
    }

    // ================= 2. 配置标准化器 =================
    DataNormalizer normalizer;
    normalizer.setInputUnit(DataNormalizer::Field::OilPressure,    QStringLiteral("psi"));
    normalizer.setInputUnit(DataNormalizer::Field::CasingPressure, QStringLiteral("psi"));
    normalizer.setInputUnit(DataNormalizer::Field::DynamicLevel,   QStringLiteral("ft"));
    // 日月歧义解析顺序：默认 DayFirst（03/05/2026 -> 05-03）。
    // 若你的数据来自美式 Excel 导出，打开下面这行即可切成 月/日：
    // normalizer.setDateOrder(DataNormalizer::DateOrder::MonthFirst);

    // ================= 3. 跑标准化 =================
    DataNormalizer::Report report;
    const QVector<RawWellData> clean = normalizer.normalize(raw, &report);

    // ================= 4. 把结果摊开显示 =================
    QString text;
    QTextStream ts(&text);
    const QString line(QStringLiteral("────────────────────────────────────────────────────────────────\n"));

    ts << QStringLiteral("输入 ") << raw.size() << QStringLiteral(" 条")
       << QStringLiteral(" | 油压/套压 = psi，动液面 = ft")
       << QStringLiteral(" | 日期歧义按「日/月」解释\n");
    ts << line << QStringLiteral("\n");

    for (int i = 0; i < clean.size(); ++i) {
        const RawWellData& d = clean.at(i);

        ts << QStringLiteral("【") << (i + 1) << QStringLiteral("】井号 ") << d.wellName
           << QStringLiteral(" | 层位 ") << d.layer
           << QStringLiteral(" | 日期 ") << d.date << QStringLiteral("\n");

        ts << QStringLiteral("     时间 ") << DataNormalizer::formatNumber(d.productionTime, 2)
           << QStringLiteral(" h | 液 ")  << DataNormalizer::formatNumber(d.dailyLiquid, 2)
           << QStringLiteral(" t/d | 油 ") << DataNormalizer::formatNumber(d.dailyOil, 2)
           << QStringLiteral(" t/d | 水 ") << DataNormalizer::formatNumber(d.dailyWater, 2)
           << QStringLiteral(" t/d | 气 ") << DataNormalizer::formatNumber(d.dailyGas, 0)
           << QStringLiteral(" m3/d\n");

        ts << QStringLiteral("     油压 ") << DataNormalizer::formatNumber(d.oilPressure, 2)
           << QStringLiteral(" MPa | 套压 ") << DataNormalizer::formatNumber(d.casingPressure, 2)
           << QStringLiteral(" MPa | 动液面 ") << DataNormalizer::formatNumber(d.dynamicLevel, 2)
           << QStringLiteral(" m | 注水 ") << DataNormalizer::formatNumber(d.dailyInjection, 2)
           << QStringLiteral(" m3/d\n");

        if (d.isAnomaly)
            ts << QStringLiteral("     [异常] ") << d.anomalyMessage << QStringLiteral("\n");
        else
            ts << QStringLiteral("     [正常]\n");
        ts << QStringLiteral("\n");
    }

    ts << line;
    ts << report.toString() << QStringLiteral("\n");
    ts.flush();

    m_output->setPlainText(text);

    // 同时打到 stderr，在 Qt Creator 的「应用程序输出」里也能看到（GUI 程序没有控制台）
    qDebug().noquote() << text;
}

// ---------------------------------------------------------------------------
// 数值文本解析 / 格式化演示
// 场景：从 Excel / CSV 读进来的单元格都是文本，直接 toDouble() 会把
//       "1,234.5" / "１２００" / "1.2万" 统统变成 0，这里演示正确的解析姿势
// ---------------------------------------------------------------------------
void Widget::runNumberParsing()
{
    const QVector<QPair<QString, QString> > cases = {
        { QStringLiteral("1,234.5"), QStringLiteral("千分位逗号") },
        { QStringLiteral("1 234.5"), QStringLiteral("千分位空格") },
        { QStringLiteral("１２３４"),  QStringLiteral("全角数字") },
        { QStringLiteral("－１２３"),  QStringLiteral("全角负号") },
        { QStringLiteral("(123.5)"), QStringLiteral("会计式负数") },
        { QStringLiteral("1.5e3"),   QStringLiteral("科学计数法") },
        { QStringLiteral("1.2万"),   QStringLiteral("中文量级词") },
        { QStringLiteral("  88.0 "), QStringLiteral("首尾空白") },
        { QStringLiteral("NA"),      QStringLiteral("缺失标记 -> 拒绝，而不是当 0") },
        { QStringLiteral("-"),       QStringLiteral("缺失标记 -> 拒绝") },
        { QStringLiteral("1.2.3"),   QStringLiteral("畸形数字 -> 拒绝") },
        { QStringLiteral("1e400"),   QStringLiteral("溢出 -> 拒绝") },
    };

    QString text;
    QTextStream ts(&text);

    ts << QStringLiteral("数值文本解析：DataNormalizer::parseNumber(text, &v)\n");
    ts << QStringLiteral("──────────────────────────────────────────────────────────────\n");
    ts << QStringLiteral("输入文本        结果              说明\n\n");

    for (const QPair<QString, QString>& c : cases) {
        double v = 0.0;
        QString why;
        const bool ok = DataNormalizer::parseNumber(c.first, &v,
                                                    DataNormalizer::NumberTextRule(), &why);
        const QString result = ok
            ? (QStringLiteral("OK   ") + DataNormalizer::formatNumber(v, 4, true))
            : QStringLiteral("FAIL");
        ts << c.first.leftJustified(16, QLatin1Char(' '))
           << result.leftJustified(18, QLatin1Char(' '))
           << (ok ? c.second : why)
           << QStringLiteral("\n");
    }

    ts << QStringLiteral("\n数值格式化：DataNormalizer::formatNumber(value, decimals, trimZeros)\n");
    ts << QStringLiteral("──────────────────────────────────────────────────────────────\n");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    ts << QStringLiteral("formatNumber(10.0, 2, false)  -> ") << DataNormalizer::formatNumber(10.0, 2, false) << QStringLiteral("\n");
    ts << QStringLiteral("formatNumber(10.0, 2, true)   -> ") << DataNormalizer::formatNumber(10.0, 2, true)  << QStringLiteral("\n");
    ts << QStringLiteral("formatNumber(10.5, 2, true)   -> ") << DataNormalizer::formatNumber(10.5, 2, true)  << QStringLiteral("\n");
    ts << QStringLiteral("formatNumber(-0.0, 3, false)  -> ") << DataNormalizer::formatNumber(-0.0, 3, false) << QStringLiteral("   （-0 归一）\n");
    ts << QStringLiteral("formatNumber(NaN, 2)          -> ") << DataNormalizer::formatNumber(nan, 2)         << QStringLiteral("\n");
    ts << QStringLiteral("formatNumber(+Inf, 2)         -> ") << DataNormalizer::formatNumber(inf, 2)         << QStringLiteral("\n");
    ts << QStringLiteral("isFiniteNumber(+Inf)          -> ")
       << (DataNormalizer::isFiniteNumber(inf) ? QStringLiteral("true") : QStringLiteral("false"))
       << QStringLiteral("\n");

    ts.flush();
    m_output->setPlainText(text);
    qDebug().noquote() << text;
}
