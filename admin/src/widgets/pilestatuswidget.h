#ifndef PILESTATUSWIDGET_H
#define PILESTATUSWIDGET_H

#include <QWidget>
#include <QString>
#include <QJsonObject>

class QShowEvent;
class QFrame;
class QLabel;
class QPushButton;
class PileStatusModel;

/**
 * @brief 电桩状态概览页（PC 运营后台）
 *
 * 职责：展示电桩状态概览的 5 个统计卡片：
 *   上面一行 3 个：总桩数 / 在用桩 / 闲置桩
 *   下面一行 2 个（居中）：故障桩 / 在线率
 * 所有展示数据均从 PileStatusModel 的 dataReady(data) 读取，UI 不硬编码数据。
 *
 * 刷新逻辑：
 *   - showEvent 中调用 fetchData()，每次切到该页自动刷新；
 *   - 右上角"刷新"按钮点击后调用 fetchData()。
 *
 * 卡片样式与 DashboardWidget 一致（深色科技风：
 * 背景 #0f1b2d，边框 #1e2d45，圆角 8px，内边距 16px）。
 */
class PileStatusWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PileStatusWidget(QWidget *parent = nullptr);

    /**
     * @brief 设置管理员 Token（由 MainWindow 调用）
     * 内部转发给 PileStatusModel::setAuthToken()；首次拉取由 showEvent 触发。
     */
    void setAuthToken(const QString &token);

protected:
    /** 每次页面显示（切换菜单进入本页）时自动刷新数据 */
    void showEvent(QShowEvent *event) override;

private slots:
    void onDataReady(const QJsonObject &data);   // 解析 data 字段并刷新卡片
    void onErrorOccurred(const QString &msg);    // 弹出错误提示

private:
    void buildUi();     // 构建界面骨架与样式（纯代码布局，无 .ui 文件）
    void refreshCards();// 从缓存 data 刷新 5 个卡片

    // 单个卡片构造辅助：title -> [QFrame, 数值 QLabel, 副标题 QLabel]
    QFrame *makeCard(const QString &title, const QString &objectName,
                     QLabel **valueLabel, QLabel **subLabel);

    PileStatusModel *m_model;   // 数据源（唯一数据入口）
    QPushButton *m_btnRefresh = nullptr;

    // 卡片 1：总桩数
    QLabel *m_lblTotal = nullptr;
    // 卡片 2：在用桩
    QLabel *m_lblInUse = nullptr;
    QLabel *m_lblInUseSub = nullptr;
    // 卡片 3：闲置桩
    QLabel *m_lblIdle = nullptr;
    QLabel *m_lblIdleSub = nullptr;
    // 卡片 4：故障桩
    QLabel *m_lblFault = nullptr;
    QLabel *m_lblFaultSub = nullptr;
    // 卡片 5：在线率
    QLabel *m_lblRate = nullptr;
    QLabel *m_lblRateSub = nullptr;

    QJsonObject m_lastData;     // 最近一次成功拉取的数据（供刷新时复用）
};

#endif // PILESTATUSWIDGET_H
