#ifndef THEME_H
#define THEME_H

#include <QString>

// 用户端主题（公共文件，由 A 维护）—— 手机风格浅色主题，绿色 = 充电
namespace Theme {

const char *const Bg     = "#F5F7F6";
const char *const Card   = "#FFFFFF";
const char *const Line   = "#E3E8E5";
const char *const Text   = "#1C2422";
const char *const Muted  = "#6A7672";
const char *const Green  = "#0E8A57";
const char *const GreenD = "#0B6B44";
const char *const Red    = "#D93025";
const char *const Orange = "#E8871A";

inline QString qss()
{
    return QString(R"(
QWidget            { background:%1; color:%4; font-size:14px; }
QWidget#Card       { background:%2; border:1px solid %3; border-radius:12px; }
QLabel             { background:transparent; }
QWidget#Nav        { background:%2; border-top:1px solid %3; }

QLabel#H1          { font-size:19px; font-weight:700; }
QLabel#Cap         { color:%5; font-size:12px; }
QLabel#Big         { color:%6; font-size:30px; font-weight:700; }
QLabel#Money       { color:%6; font-size:20px; font-weight:700; }
QLabel#Warn        { color:%8; font-size:13px; }

QPushButton        { background:%6; color:#FFFFFF; border:none; border-radius:8px;
                     padding:11px 16px; font-weight:700; }
QPushButton:hover  { background:%7; }
QPushButton:disabled { background:%3; color:%5; }
QPushButton#Ghost  { background:transparent; color:%6; border:1px solid %6; }
QPushButton#Danger { background:transparent; color:%9; border:1px solid %9; }
QPushButton#NavBtn { background:transparent; color:%5; border:none; border-radius:0;
                     padding:9px; font-size:12px; font-weight:500; }
QPushButton#NavBtn:checked { color:%6; font-weight:700; }

QComboBox          { background:%2; border:1px solid %3; border-radius:8px; padding:8px 10px; }
QComboBox QAbstractItemView { background:%2; }
QListWidget        { background:transparent; border:none; }
QListWidget::item  { background:%2; border:1px solid %3; border-radius:10px;
                     margin:4px 2px; padding:10px; }
QListWidget::item:selected { border:2px solid %6; color:%4; }
QScrollArea        { background:transparent; border:none; }
QScrollBar:vertical { background:transparent; width:7px; }
QScrollBar::handle:vertical { background:%3; border-radius:3px; min-height:30px; }
QScrollBar::add-line, QScrollBar::sub-line { height:0; width:0; }
QProgressBar       { background:%3; border:none; border-radius:6px; height:12px; text-align:center;
                     color:transparent; }
QProgressBar::chunk { background:%6; border-radius:6px; }
)")
        .arg(Bg, Card, Line, Text, Muted, Green, GreenD, Orange, Red);
}

} // namespace Theme

#endif // THEME_H