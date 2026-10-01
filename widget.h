#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>

class QPlainTextEdit;

class Widget : public QWidget
{
    Q_OBJECT

public:
    explicit Widget(QWidget *parent = nullptr);
    ~Widget() override;

private slots:
    void runNormalize();
    void runNumberParsing();

private:
    QPlainTextEdit *m_output = nullptr;
};

#endif // WIDGET_H
