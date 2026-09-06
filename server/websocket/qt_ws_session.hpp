#pragma once

#include "ws_manager.hpp"
#include <QObject>
#include <QTcpSocket>
#include <QByteArray>
#include <QtCore/qglobal.h>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
using qsizetype = int;
#endif
#include <boost/beast/http.hpp>
#include <memory>
#include <string>
#include <atomic>

namespace ev {

namespace http = boost::beast::http;

class QtWsSession : public QObject, public IWebSocketSession, public std::enable_shared_from_this<QtWsSession> {
    Q_OBJECT

public:
    explicit QtWsSession(QTcpSocket* socket, QObject* parent = nullptr);
    ~QtWsSession() override;

    // 启动 WebSocket 会话并执行 RFC 6455 握手
    void start(const http::request<http::string_body>& req);

    // 实现 IWebSocketSession 接口
    void send_json(const std::string& json_text) override;
    bool is_open() const override;

private slots:
    void onReadyRead();
    void onDisconnected();

private:
    void perform_handshake(const http::request<http::string_body>& req);
    void route_subscription(std::string_view target);
    void parse_incoming_frames();
    void handle_text_payload(std::string_view payload);
    static QByteArray build_websocket_frame(std::string_view payload, uint8_t opcode = 0x01);

    QTcpSocket* socket_{nullptr};
    QByteArray read_buffer_;
    std::atomic<bool> is_open_{false};
};

} // namespace ev
