#ifndef SESSION_H
#define SESSION_H

#include <QString>

// 当前登录用户（云端版）—— 除了 userId 还要保存 server 发的 access token
//   B（登录页）登录成功后 setUserId() + setToken()
//   ApiClient 发请求时自动带上 token
class Session
{
public:
    static Session &i()
    {
        static Session s;
        return s;
    }
    int     userId() const { return m_userId; }
    void    setUserId(int id) { m_userId = id; }
    QString token() const { return m_token; }
    void    setToken(const QString &t) { m_token = t; }

private:
    Session() = default;
    int     m_userId = 0;
    QString m_token;
};

#endif // SESSION_H
