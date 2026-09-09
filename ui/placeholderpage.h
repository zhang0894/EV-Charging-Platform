#ifndef PLACEHOLDERPAGE_H
#define PLACEHOLDERPAGE_H

#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

// 页面还没写好之前的占位
class PlaceholderPage : public QWidget
{
    Q_OBJECT
public:
    explicit PlaceholderPage(const QString &text, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        auto *lay = new QVBoxLayout(this);
        auto *lab = new QLabel(text, this);
        lab->setObjectName(QStringLiteral("Cap"));
        lab->setAlignment(Qt::AlignCenter);
        lab->setWordWrap(true);
        lay->addWidget(lab);
    }
};

#endif // PLACEHOLDERPAGE_H