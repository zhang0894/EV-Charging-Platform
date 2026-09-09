#ifndef LOGINWINDOW_H
#define LOGINWINDOW_H

#include <QWidget>
class QLineEdit; class QPushButton; class QLabel; class QTimer;
class LoginWindow : public QWidget {
    Q_OBJECT
public:
    explicit LoginWindow(QWidget *parent = nullptr);
signals:
    void loginSucceeded(bool isNewUser);
private slots:
    void sendCode();
    void login();
    void toggleLoginMode();
    void continueWithPhone();
private:
    void showRegistration();
    void completeLogin(const QString &expectedPhone, bool isNewUser);
    QLineEdit *phone, *code, *password, *confirmPassword;
    QPushButton *send, *submit, *modeSwitch; QLabel *tip; QTimer *timer; int left = 0;
    bool registering = false;
    bool passwordMode = false;
    bool phoneOnly = true;
    QString issuedCode;
};
#endif
