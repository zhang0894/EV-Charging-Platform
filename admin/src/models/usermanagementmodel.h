#ifndef USERMANAGEMENTMODEL_H
#define USERMANAGEMENTMODEL_H

#include <QObject>
#include <QStandardItemModel>
#include <QString>
#include <QJsonArray>

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QJsonObject;
QT_END_NAMESPACE

/**
 * @brief 用户管理数据模型（PC 运营后台 - 用户管理模块）
 *
 * 继承自 QObject，内部持有一个 QStandardItemModel 作为表格数据源
 * （每行一个用户），通过 getModel() 提供给 Widget 绑定 QTableView。
 *
 * 接口对应《端口设计文档》3.5 节：
 *   - GET /api/v1/admin/users                       分页查询用户列表
 *       查询参数: page / page_size / phone(可选) / status(可选, 1=正常 2=冻结)
 *   - PUT /api/v1/admin/users/{user_id}/status      冻结/解冻用户
 *       Body: { "status": 2, "reason": "..." }
 *
 * 统一响应信封：{ code, msg, data, timestamp }，code==0 表示成功。
 * 响应解析失败 / 网络错误时通过 errorOccurred() 信号通知界面，
 * 成功时通过 usersReady() / operationSuccess() 通知界面刷新。
 *
 * 表格列顺序（与文档字段对应）：
 *   用户ID | 手机号 | 昵称 | 余额(元) | 状态(正常/冻结) | 注册时间 | 操作(占位)
 *   - 操作列不存放按钮，按钮由 Widget 依据 StatusRole/UserIdRole 动态安装；
 *   - 每行 UserRole 附带原始数据：UserIdRole / StatusRole / PhoneRole。
 */
class UserManagementModel : public QObject
{
    Q_OBJECT
public:
    /** 列索引：用户ID / 手机号 / 昵称 / 余额 / 状态 / 注册时间 / 操作 */
    enum Column {
        UserIdCol = 0,
        PhoneCol,
        NicknameCol,
        BalanceCol,
        StatusCol,
        CreatedAtCol,
        ActionCol,
        ColCount = 7
    };

    /** 行自定义数据角色（Widget 构建操作按钮时读取） */
    enum DataRole {
        UserIdRole = Qt::UserRole + 1,   // 用户 ID（int）
        StatusRole   = Qt::UserRole + 2, // 用户状态（int, 1=正常 2=冻结）
        PhoneRole    = Qt::UserRole + 3  // 手机号（QString）
    };

    explicit UserManagementModel(QObject *parent = nullptr);

    /** 获取表格数据源（供 Widget 绑定 QTableView） */
    QStandardItemModel *getModel();

    /**
     * @brief 设置管理员鉴权 Token（Bearer Token，由 MainWindow 传入）
     * 仅保存 Token，不自动发起请求；首次拉取由 Widget 调用 fetchUsers() 触发。
     */
    void setAuthToken(const QString &token);

    /**
     * @brief 分页查询用户列表
     * @param page 当前页码（从 1 开始）
     * @param pageSize 每页条数（默认 10）
     * @param phoneFilter 手机号模糊搜索（空串则不携带该参数）
     * @param statusFilter 状态筛选（-1=全部不携带, 1=正常, 2=冻结）
     */
    void fetchUsers(int page = 1, int pageSize = 10,
                    const QString &phoneFilter = QString(),
                    int statusFilter = -1);

    /**
     * @brief 冻结/解冻用户
     * @param userId 目标用户 ID
     * @param newStatus 目标状态（2=冻结, 1=解冻恢复正常）
     * @param reason 操作原因（记录到后台审计日志）
     */
    void setUserStatus(int userId, int newStatus, const QString &reason);

signals:
    /** 用户列表已就绪并填充进 Model，Widget 据此更新分页栏与操作按钮 */
    void usersReady(const QJsonArray &users, int total, int page, int pageSize);

    /** 冻结/解冻操作成功（msg 为可展示的提示信息） */
    void operationSuccess(const QString &msg);

    /** 网络请求失败 / 响应解析失败 / 业务错误码非 0 */
    void errorOccurred(const QString &errorMsg);

private:
    /** 懒初始化 QNetworkAccessManager（以 this 为 parent，随 Model 释放） */
    void ensureNetworkManager();

    /** 为请求填充公共头（Content-Type / Accept / Authorization） */
    void prepareRequest(QNetworkRequest *request) const;

    /** 将用户数组填充进表格 Model（清空旧行后逐行追加） */
    void populateUsers(const QJsonArray &users);

    /** 处理用户列表响应：校验 -> 解析 -> 填表 -> 发 usersReady */
    void handleUsersReply(QNetworkReply *reply);

    /** 处理冻结/解冻响应：校验 -> 发 operationSuccess */
    void handleStatusReply(QNetworkReply *reply, int userId, int newStatus);

    /**
     * @brief 解析统一响应信封 {code,msg,data}
     * @return true 表示 code==0 且 data 已取出；false 表示失败（已 emit errorOccurred）
     * 解析失败时在日志中打印原始响应，便于排查文档与实现不一致的情况。
     */
    bool extractData(const QByteArray &body, const QString &apiTag, QJsonObject &outData);

    QStandardItemModel *m_tableModel = nullptr; // 表格数据源
    QNetworkAccessManager *m_networkManager = nullptr; // HTTP 请求管理器（懒创建）
    QString m_authToken;                        // 管理员 Bearer Token

    // 服务器地址：当前写死，后续再改为可配置
    const QString m_serverBase = QStringLiteral("http://62.234.84.145:8080");

    // 当前查询上下文（响应到达时用于兜底默认值）
    int m_page = 1;
    int m_pageSize = 10;
    QString m_phoneFilter;
    int m_statusFilter = -1;
};

#endif // USERMANAGEMENTMODEL_H
