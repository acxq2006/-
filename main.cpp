/**
 * 02_demo —— DataNormalizer 图形界面验证程序
 *
 * 界面上有两个按钮：
 *   · 跑一遍标准化     4 条脏数据 -> normalize() -> 结果 + 诊断报告
 *   · 跑数值文本解析   parseNumber / formatNumber 的边界用例
 *
 * 输出既写进窗口里的文本框，也走 qDebug 到「应用程序输出」面板（方便复制）。
 * 模块本身只依赖 Qt Core，界面代码可以随时删掉，不影响 DataNormalizer。
 */
#include "widget.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    Widget w;
    w.show();

    return app.exec();
}
