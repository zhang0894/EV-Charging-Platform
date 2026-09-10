#include "ui/firstprofilesetuppage.h"
#include "core/userservice.h"
#include "core/session.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>
FirstProfileSetupPage::FirstProfileSetupPage(QWidget *p):QWidget(p){setObjectName("SetupWindow");setWindowTitle(QStringLiteral("完善个人资料"));setFixedSize(420,760);auto*l=new QVBoxLayout(this);l->setContentsMargins(34,110,34,56);l->setSpacing(14);auto*t=new QLabel(QStringLiteral("完善个人资料"));t->setObjectName("H1");l->addWidget(t);auto*h=new QLabel(QStringLiteral("昵称和头像均可稍后修改，不填写昵称将使用默认昵称。"));h->setObjectName("Cap");l->addWidget(h);auto*r=new QHBoxLayout;m_avatar=new QLabel(QStringLiteral("默认头像"));m_avatar->setAlignment(Qt::AlignCenter);m_avatar->setFixedSize(90,90);r->addWidget(m_avatar);auto*b=new QPushButton(QStringLiteral("选择头像"));b->setObjectName("Ghost");r->addWidget(b);r->addStretch();l->addLayout(r);m_name=new QLineEdit;m_name->setPlaceholderText(QStringLiteral("昵称（可选，默认用户+手机号后四位）"));l->addWidget(m_name);auto*saveBtn=new QPushButton(QStringLiteral("完成并进入电站列表"));saveBtn->setMinimumHeight(46);l->addWidget(saveBtn);m_tip=new QLabel;m_tip->setObjectName("Warn");l->addWidget(m_tip);l->addStretch();connect(b,&QPushButton::clicked,this,&FirstProfileSetupPage::chooseAvatar);connect(saveBtn,&QPushButton::clicked,this,&FirstProfileSetupPage::save);}
void FirstProfileSetupPage::chooseAvatar(){const auto f=QFileDialog::getOpenFileName(this,QStringLiteral("选择头像"),{},QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp)"));if(f.isEmpty())return;if(QFileInfo(f).size()>1LL*1024*1024){m_tip->setText(QStringLiteral("头像图片不能超过 1 MB"));return;}m_file=f;m_avatar->setPixmap(QPixmap(f).scaled(90,90,Qt::KeepAspectRatioByExpanding,Qt::SmoothTransformation));}
void FirstProfileSetupPage::save(){
    const auto name=m_name->text().trimmed();
    QString e;
    // 昵称可留空；服务端注册时已按“用户+手机号后四位”生成默认昵称。
    if(!name.isEmpty()&&!UserService::updateProfile(name,&e)){m_tip->setText(e);return;}
    // 未选择头像时，将内置默认头像上传到云端，确保换设备仍能显示同一头像。
    const QString avatarFile=m_file.isEmpty()?QStringLiteral(":/assets/assets/default.jpeg"):m_file;
    if(!UserService::uploadAvatar(avatarFile,nullptr,&e)){m_tip->setText(e);return;}
    hide();emit completed();
}
