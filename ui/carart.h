#ifndef CARART_H
#define CARART_H

#include "ui/theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QWidget>

// 充电中的小插画：一辆薄荷绿的车 + 一根充电桩，纯 QPainter 画，不依赖图片/SVG 模块
class CarArt : public QWidget
{
public:
    explicit CarArt(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(110);
        setMinimumWidth(260);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const double W = width(), H = height();
        const double cx = W / 2.0 - 30, ground = H - 14;
        const QColor body(Theme::Green), bodyD(Theme::GreenD), glass("#DDF5EF"), dark("#2B3A36");

        // 地面阴影
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 18));
        p.drawEllipse(QRectF(cx - 105, ground - 6, 230, 14));

        // 车身：下半矩形 + 上半弧形车顶
        QPainterPath car;
        car.moveTo(cx - 100, ground - 8);
        car.lineTo(cx - 100, ground - 30);
        car.quadTo(cx - 100, ground - 44, cx - 86, ground - 46);   // 前脸
        car.lineTo(cx - 60, ground - 48);
        car.quadTo(cx - 30, ground - 84, cx + 20, ground - 84);    // 车顶
        car.quadTo(cx + 60, ground - 84, cx + 84, ground - 50);    // 后挡风
        car.lineTo(cx + 100, ground - 46);
        car.quadTo(cx + 108, ground - 44, cx + 108, ground - 30);
        car.lineTo(cx + 108, ground - 8);
        car.closeSubpath();
        p.setBrush(body);
        p.drawPath(car);
        // 车窗
        QPainterPath win;
        win.moveTo(cx - 52, ground - 50);
        win.quadTo(cx - 28, ground - 78, cx + 18, ground - 78);
        win.quadTo(cx + 52, ground - 78, cx + 72, ground - 50);
        win.closeSubpath();
        p.setBrush(glass);
        p.drawPath(win);
        p.setBrush(body);
        p.drawRect(QRectF(cx + 8, ground - 78, 5, 28));            // 窗框
        // 车轮
        p.setBrush(dark);
        p.drawEllipse(QPointF(cx - 60, ground - 8), 15, 15);
        p.drawEllipse(QPointF(cx + 62, ground - 8), 15, 15);
        p.setBrush(QColor("#9FB4AE"));
        p.drawEllipse(QPointF(cx - 60, ground - 8), 6, 6);
        p.drawEllipse(QPointF(cx + 62, ground - 8), 6, 6);
        // 车灯 / 充电口
        p.setBrush(QColor("#FFF3C4"));
        p.drawRoundedRect(QRectF(cx - 100, ground - 40, 10, 6), 2, 2);
        p.setBrush(bodyD);
        p.drawRoundedRect(QRectF(cx + 96, ground - 38, 8, 8), 2, 2);

        // 充电桩
        const double px = cx + 150;
        p.setBrush(QColor("#E8F0ED"));
        p.drawRoundedRect(QRectF(px - 16, ground - 86, 32, 86), 6, 6);
        p.setBrush(body);
        p.drawRoundedRect(QRectF(px - 11, ground - 78, 22, 26), 4, 4);   // 屏幕
        // 闪电
        QPainterPath bolt;
        bolt.moveTo(px + 2, ground - 76); bolt.lineTo(px - 6, ground - 64); bolt.lineTo(px, ground - 64);
        bolt.lineTo(px - 2, ground - 55); bolt.lineTo(px + 6, ground - 67); bolt.lineTo(px, ground - 67);
        bolt.closeSubpath();
        p.setBrush(Qt::white);
        p.drawPath(bolt);
        // 充电线：从桩到车尾充电口
        QPainterPath cable;
        cable.moveTo(px - 16, ground - 46);
        cable.cubicTo(px - 40, ground - 46, px - 40, ground - 34, cx + 104, ground - 34);
        p.setPen(QPen(dark, 3, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawPath(cable);
    }
};

#endif // CARART_H
