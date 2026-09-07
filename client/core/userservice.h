#ifndef USERSERVICE_H
#define USERSERVICE_H

#include <QString>

struct CloudUser { int id = 0; QString phone, nickname, avatarUrl, refreshToken; bool isNew = false; };
struct UserProfile { QString phone, nickname, avatarUrl, createdAt; double balance = 0; };
namespace UserService {
bool login(const QString &phone, const QString &code, CloudUser *user, QString *error = nullptr);
void loginAsync(const QString &phone, const QString &code,
                std::function<void(bool, const CloudUser &, const QString &)> callback);
// 账号密码登录：复用统一登录接口，auth_type=password。
void passwordLoginAsync(const QString &phone, const QString &password,
                        std::function<void(bool, const CloudUser &, const QString &)> callback);
void refreshTokenAsync(const QString &refreshToken,
                       std::function<void(bool, const CloudUser &, const QString &)> callback);
void checkPhoneAsync(const QString &phone, std::function<void(bool, bool, const QString &)> callback);
void registerAsync(const QString &phone, const QString &password,
                   std::function<void(bool, const CloudUser &, const QString &)> callback);
bool updateProfile(const QString &nickname, QString *error = nullptr);
bool uploadAvatar(const QString &filePath, QString *avatarUrl, QString *error = nullptr);
void profileAsync(std::function<void(bool, const UserProfile &, const QString &)> callback);
void rechargeAsync(double amount, std::function<void(bool, double, const QString &)> callback);
void changePasswordAsync(const QString &oldPassword, const QString &newPassword,
                         std::function<void(bool, const QString &)> callback);
}
#endif
