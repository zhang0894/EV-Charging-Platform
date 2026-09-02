#ifndef SESSION_H
#define SESSION_H

// 当前登录用户 —— A/B 的对接点：
//   B（登录页）登录成功后调用一次 setUserId()
//   A（充电流程）只读 userId()

class Session
{
public:
    static Session &i()
    {
        static Session s;
        return s;
    }
    int userId() const { return m_userId; }
    void setUserId(int id) { m_userId = id;}

private:
    Session() = default;
    int m_userId = 0;
};

#endif // SESSION_H
