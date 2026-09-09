#include "ui/orderspage.h"

#include "core/chargeservice.h"
#include "ui/receipttext.h"

#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

OrdersPage::OrdersPage(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(14, 14, 14, 14);
    auto *title = new QLabel(QStringLiteral("我的订单"), this);
    title->setObjectName(QStringLiteral("H1"));
    lay->addWidget(title);
    auto *cap = new QLabel(QStringLiteral("点一条订单查看小票"), this);
    cap->setObjectName(QStringLiteral("Cap"));
    lay->addWidget(cap);
    m_list = new QListWidget(this);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setWordWrap(true);
    // 手机宽度就 420，禁止横向滚动，让长站名换行
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lay->addWidget(m_list, 1);

    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QVariant id = item->data(Qt::UserRole);
        if (id.isValid())
            showDetail(id.toString());
    });
}

void OrdersPage::refresh()
{
    m_list->clear();
    const auto rows = ChargeService::orderHistory();
    if (rows.isEmpty()) {
        new QListWidgetItem(QStringLiteral("暂无充电记录"), m_list);
        return;
    }
    // 长站名会换行，QListWidget 默认行高算不准 → 按换行后的实际高度设 sizeHint
    const QFontMetrics fm(m_list->font());
    const int wrapWidth = qMax(300, m_list->viewport()->width() - 44);
    for (const ChargeService::OrderRow &r : rows) {
        const QString text =
            QStringLiteral("%1 · %2（%3）\n%4 · %5 · %6 度\n￥%7 · %8")
                .arg(r.stationName, r.pileCode, r.typeText, r.startTime,
                     durationText(r.durationSec)).arg(r.kwh, 0, 'f', 2)
                .arg(r.amount, 0, 'f', 2).arg(r.statusText);
        auto *item = new QListWidgetItem(text, m_list);
        const int h = fm.boundingRect(QRect(0, 0, wrapWidth, 1000),
                                      Qt::TextWordWrap, text).height();
        item->setSizeHint(QSize(0, h + 32));
        item->setData(Qt::UserRole, r.id);
    }

    // 截图自检：NCS_SHOT_ORDER=1 时自动打开第一条订单的小票
    if (!qEnvironmentVariableIsEmpty("NCS_SHOT_ORDER")) {
        const QString first = rows.first().id;
        QTimer::singleShot(300, this, [this, first] { showDetail(first); });
    }
}

// 订单小票详情（点击查看小票）
void OrdersPage::showDetail(const QString &orderId)
{
    ChargeService::Receipt r;
    if (!ChargeService::orderDetail(orderId, &r))
        return;

    auto *dlg = new QDialog(this);
    dlg->setWindowTitle(QStringLiteral("订单小票"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setFixedWidth(340);
    auto *lay = new QVBoxLayout(dlg);
    auto *card = new QWidget(dlg);
    card->setObjectName(QStringLiteral("Card"));
    auto *cl = new QVBoxLayout(card);
    auto *text = new QLabel(receiptHtml(r, true), card);
    text->setTextFormat(Qt::RichText);
    text->setWordWrap(true);
    cl->addWidget(text);
    lay->addWidget(card);
    auto *close = new QPushButton(QStringLiteral("关闭"), dlg);
    connect(close, &QPushButton::clicked, dlg, &QDialog::accept);
    lay->addWidget(close);

    if (!qEnvironmentVariableIsEmpty("NCS_SHOT")) {   // 截图自检：抓小票后退出
        const QString file = qEnvironmentVariable("NCS_SHOT");
        QTimer::singleShot(500, dlg, [dlg, file] {
            dlg->grab().save(file);
            qApp->quit();
        });
    }
    // 用 show() 不用 open()：open() 的窗口模态在 macOS 会变成原生 sheet，
    // 会吞掉截图自检里的 qApp->quit()，程序退不出来
    dlg->show();
}
