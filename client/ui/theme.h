#ifndef THEME_H
#define THEME_H

#include <QString>

// 用户端主题（公共文件）—— 薄荷绿手机风格：淡绿渐变底 + 白色圆角卡片 + 胶囊按钮
namespace Theme {

const char *const Bg      = "#F3FAF8";   // 页面底色（渐变的下端）
const char *const BgTop   = "#D9F2EA";   // 渐变的上端
const char *const Card    = "#FFFFFF";
const char *const Line    = "#E4EEEA";
const char *const Text    = "#1E2B28";
const char *const Muted   = "#8A9994";
const char *const Green   = "#3DBBA0";   // 主色（充电 / 主按钮）
const char *const GreenD  = "#2E9E86";
const char *const GreenL  = "#CDEDE4";   // 进度条底、刻度未充部分
const char *const Red     = "#E0605A";
const char *const Orange  = "#EDA33A";
const char *const BtnGrey = "#EEF2F0";   // 次要按钮底
const char *const BtnGreyText = "#9AA8A3";

inline QString qss()
{
    return QString(R"(
QWidget            { background:transparent; color:%4; font-size:14px; }
QWidget#MainWindow, QWidget#LoginWindow, QWidget#SetupWindow, QDialog, QMessageBox, QInputDialog {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %10, stop:0.5 %1, stop:1 #FFFFFF);
}
QWidget#Card       { background:%2; border:1px solid %3; border-radius:16px; }
QLabel             { background:transparent; }
QWidget#Nav        { background:%2; border-top:1px solid %3; }

QLabel#Brand       { color:%6; font-size:26px; font-weight:800; }
QLabel#H1          { font-size:20px; font-weight:700; }
QLabel#CardTitle   { font-size:16px; font-weight:700; }
QLabel#Cap         { color:%5; font-size:12px; }
QLabel#Big         { color:%4; font-size:32px; font-weight:700; }
QLabel#Money       { color:%6; font-size:20px; font-weight:700; }
QLabel#Warn        { color:%8; font-size:12px; }
QLabel#Stat        { font-size:22px; font-weight:700; }
QLabel#StatCap     { color:%5; font-size:12px; }
QLabel#OrderNo     { color:%4; font-size:13px; }

QPushButton        { background:%6; color:#FFFFFF; border:none; border-radius:24px;
                     min-height:48px; padding:0 20px; font-size:16px; font-weight:600; }
QPushButton:hover  { background:%7; }
QPushButton:pressed{ background:%7; }
QPushButton:disabled { background:%3; color:%5; }
QPushButton#Secondary { background:%11; color:%12; }
QPushButton#Ghost  { background:transparent; color:%6; border:1px solid %6; min-height:32px;
                     border-radius:16px; padding:0 14px; font-size:13px; }
QPushButton#Small  { background:transparent; color:%6; border:1px solid %6; min-height:20px;
                     border-radius:6px; padding:0 6px; font-size:11px; font-weight:500; }
QPushButton#Danger { background:transparent; color:%9; border:1px solid %9; }
QPushButton#NavBtn { background:transparent; color:%5; border:none; border-radius:0;
                     min-height:0; padding:10px; font-size:12px; font-weight:500; }
QPushButton#NavBtn:checked { color:%6; font-weight:700; }

QLineEdit          { background:%2; border:1px solid %3; border-radius:12px; padding:10px 12px; }
QLineEdit:focus    { border:1px solid %6; }
QComboBox          { background:%2; border:1px solid %3; border-radius:12px; padding:9px 12px; }
QComboBox QAbstractItemView { background:%2; selection-background-color:%13; }
QListWidget        { background:transparent; border:none; }
QListWidget::item  { background:%2; border:1px solid %3; border-radius:16px;
                     margin:5px 2px; padding:12px; }
QListWidget::item:selected { border:2px solid %6; color:%4; background:#F2FBF8; }
QScrollArea        { background:transparent; border:none; }
QScrollBar:vertical { background:transparent; width:6px; }
QScrollBar::handle:vertical { background:%3; border-radius:3px; min-height:30px; }
QScrollBar::add-line, QScrollBar::sub-line { height:0; width:0; }
QProgressBar       { background:%13; border:none; border-radius:6px; height:12px; text-align:center;
                     color:transparent; }
QProgressBar::chunk { background:%6; border-radius:6px; }
QDialogButtonBox QPushButton { min-height:36px; border-radius:18px; font-size:14px; }
)")
        .arg(Bg, Card, Line, Text, Muted, Green, GreenD, Orange, Red)
        .arg(BgTop, BtnGrey, BtnGreyText, GreenL);
}

} // namespace Theme

#endif // THEME_H
