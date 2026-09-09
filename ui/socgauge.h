#ifndef SOCGAUGE_H
#define SOCGAUGE_H

#include "ui/theme.h"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QWidget>
#include <QtMath>

// 电量仪表盘：270° 刻度弧，已充部分绿色、未充部分淡绿，末端一个圆点，
// 中间是大号百分比和一行小字（预计还需 X 分钟充满）
class SocGauge : public QWidget
{
public:
    explicit SocGauge(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(260, 230);
    }
    void setValue(int percent, const QString &subtitle)
    {
        m_value = qBound(0, percent, 100);
        m_subtitle = subtitle;
        update();
    }
    int value() const { return m_value; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const int side = qMin(width(), height() + 40);
        const QPointF c(width() / 2.0, side / 2.0 + 6);
        const double rOuter = side / 2.0 - 10;
        const double tickLen = 16, tickW = 3;
        constexpr int ticks = 64;
        constexpr double startDeg = 225.0, sweep = 270.0;   // 左下起，顺时针到右下

        const int lit = qRound(ticks * m_value / 100.0);
        for (int i = 0; i < ticks; ++i) {
            const double a = qDegreesToRadians(startDeg - sweep * i / (ticks - 1));
            const QPointF o(c.x() + rOuter * qCos(a), c.y() - rOuter * qSin(a));
            const QPointF in(c.x() + (rOuter - tickLen) * qCos(a),
                             c.y() - (rOuter - tickLen) * qSin(a));
            QPen pen(QColor(i < lit ? Theme::Green : Theme::GreenL), tickW, Qt::SolidLine, Qt::RoundCap);
            p.setPen(pen);
            p.drawLine(o, in);
        }
        // 末端圆点
        if (lit > 0) {
            const double a = qDegreesToRadians(startDeg - sweep * (lit - 1) / (ticks - 1));
            const QPointF d(c.x() + (rOuter - tickLen / 2) * qCos(a),
                            c.y() - (rOuter - tickLen / 2) * qSin(a));
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(Theme::Green));
            p.drawEllipse(d, 7, 7);
            p.setBrush(Qt::white);
            p.drawEllipse(d, 3, 3);
        }
        // 中间数字
        QFont big = font(); big.setPointSize(44); big.setWeight(QFont::Bold);
        QFont small = font(); small.setPointSize(16); small.setWeight(QFont::DemiBold);
        const QString num = QString::number(m_value);
        const QFontMetrics fb(big), fs(small);
        const int wNum = fb.horizontalAdvance(num), wPct = fs.horizontalAdvance(QStringLiteral("%"));
        const double x0 = c.x() - (wNum + wPct + 4) / 2.0;
        const double baseline = c.y() + fb.ascent() / 2.0 - 10;
        p.setPen(QColor(Theme::Text));
        p.setFont(big);
        p.drawText(QPointF(x0, baseline), num);
        p.setFont(small);
        p.drawText(QPointF(x0 + wNum + 4, baseline), QStringLiteral("%"));
        // 小字
        QFont cap = font(); cap.setPointSize(12);
        p.setFont(cap);
        p.setPen(QColor(Theme::Muted));
        p.drawText(QRectF(0, baseline + 8, width(), 24), Qt::AlignHCenter | Qt::AlignTop, m_subtitle);
    }

private:
    int m_value = 0;
    QString m_subtitle;
};

#endif // SOCGAUGE_H
