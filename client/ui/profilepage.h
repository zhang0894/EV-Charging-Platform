#ifndef PROFILEPAGE_H
#define PROFILEPAGE_H

#include <QWidget>
class QLabel; class QLineEdit; class QPushButton; class QNetworkAccessManager; class QTimer;
class ProfilePage : public QWidget {
    Q_OBJECT
public:
    explicit ProfilePage(QWidget *parent = nullptr);
    void reload(bool showLoading = true);
signals:
    void logoutRequested();
private slots:
    void changeAvatar();
    void editNickname();
    void changePassword();
    void recharge();
    void requireLoginNextTime();
private:
    void loadAvatar(const QString &url);
    void showDefaultAvatar();
    QLabel *m_avatar, *m_name, *m_phone, *m_balance, *m_depositNote, *m_tip;
    void applyBalance(double serverBalance);   // 显示余额 = server 余额 - 预约押金
    QLineEdit *m_amount;
    QPushButton *m_recharge, *m_changeAvatar, *m_editName, *m_changePassword, *m_logout, *m_requireLogin;
    QNetworkAccessManager *m_network;
    QTimer *m_refreshTimer;
    bool m_refreshInFlight = false;
    QString m_sessionPhone;
};
#endif
