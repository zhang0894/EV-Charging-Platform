#ifndef FIRSTPROFILESETUPPAGE_H
#define FIRSTPROFILESETUPPAGE_H
#include <QWidget>
class QLabel; class QLineEdit;
class FirstProfileSetupPage : public QWidget {
    Q_OBJECT
public:
    explicit FirstProfileSetupPage(QWidget *parent = nullptr);
signals:
    void completed();
private slots:
    void chooseAvatar();
    void save();
private:
    QString m_file; QLabel *m_avatar, *m_tip; QLineEdit *m_name;
};
#endif
