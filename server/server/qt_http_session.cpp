#include "qt_http_session.hpp"
#include "../websocket/qt_ws_session.hpp"
#include "../router/http_router.hpp"
#include <boost/beast/websocket.hpp>
#include <sstream>
#include <iostream>

namespace ev {

QtHttpSession::QtHttpSession(qintptr socketDescriptor, QObject* parent)
    : QObject(parent) {
    socket_ = new QTcpSocket(this);
    if (!socket_->setSocketDescriptor(socketDescriptor)) {
        this->deleteLater();
        return;
    }
    parser_ = std::make_unique<http::request_parser<http::string_body>>();
    parser_->body_limit(2 * 1024 * 1024); // 2MB 上限，由业务层校验 <1MB 并返回 413
    connect(socket_, &QTcpSocket::readyRead, this, &QtHttpSession::onReadyRead);
    connect(socket_, &QTcpSocket::disconnected, this, &QtHttpSession::onDisconnected);
}

QtHttpSession::~QtHttpSession() {
    if (socket_) {
        socket_->disconnect(this);
    }
}

void QtHttpSession::onReadyRead() {
    if (!socket_) return;
    QByteArray chunk = socket_->readAll();
    if (!chunk.isEmpty()) {
        raw_buffer_.append(chunk);
    }
    process_pipeline();
}

void QtHttpSession::onDisconnected() {
    this->deleteLater();
}

void QtHttpSession::process_pipeline() {
    while (!raw_buffer_.isEmpty() && parser_) {
        beast::error_code ec;
        size_t bytes_used = parser_->put(
            boost::asio::buffer(raw_buffer_.constData(), raw_buffer_.size()),
            ec
        );

        if (ec) {
            // 解析错误或请求内容超限
            if (socket_) {
                socket_->disconnectFromHost();
            }
            return;
        }

        if (bytes_used > 0) {
            raw_buffer_.remove(0, static_cast<qsizetype>(bytes_used));
        }

        if (parser_->is_done()) {
            auto req = parser_->release();

            // 1. 判断是否为 WebSocket 升级请求
            if (beast::websocket::is_upgrade(req)) {
                disconnect(socket_, &QTcpSocket::readyRead, this, &QtHttpSession::onReadyRead);
                disconnect(socket_, &QTcpSocket::disconnected, this, &QtHttpSession::onDisconnected);

                // 移交套接字至 QtWsSession (由 std::shared_ptr 独立管理生命周期，不设置 QObject parent 避免双重释放)
                auto ws = std::make_shared<QtWsSession>(socket_, nullptr);
                ws->start(req);

                socket_ = nullptr; // 套接字所有权移交给 QtWsSession
                this->deleteLater();
                return;
            }

            // 2. 正常 RESTful HTTP 请求分发
            auto res = HttpRouter::instance().dispatch(req);
            bool keep_alive = res.keep_alive();

            std::ostringstream oss;
            oss << res;
            std::string res_bytes = oss.str();

            if (socket_ && socket_->isOpen()) {
                socket_->write(res_bytes.data(), static_cast<qint64>(res_bytes.size()));
                socket_->flush();
            }

            if (!keep_alive) {
                if (socket_) {
                    socket_->disconnectFromHost();
                }
                return;
            }

            // Keep-Alive 保持连接：重置解析器以处理该连接上的下一条请求
            parser_ = std::make_unique<http::request_parser<http::string_body>>();
            parser_->body_limit(2 * 1024 * 1024);
        } else if (bytes_used == 0) {
            // 缓冲区内无足够字节推进解析状态，等待后续 readyRead 信号
            break;
        }
    }
}

} // namespace ev
