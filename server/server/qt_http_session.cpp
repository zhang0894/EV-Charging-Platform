#include "qt_http_session.hpp"
#include "business_thread_pool.hpp"
#include "../websocket/qt_ws_session.hpp"
#include "../router/http_router.hpp"
#include <boost/beast/websocket.hpp>
#include <QPointer>
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
    if (!is_processing_) {
        process_pipeline();
    }
}

void QtHttpSession::onDisconnected() {
    this->deleteLater();
}

void QtHttpSession::process_pipeline() {
    while (!raw_buffer_.isEmpty() && parser_ && !is_processing_) {
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

                // 移交套接字至 QtWsSession (由 std::shared_ptr 独立管理生命周期)
                auto ws = std::make_shared<QtWsSession>(socket_, nullptr);
                ws->start(req);

                socket_ = nullptr; // 套接字所有权移交给 QtWsSession
                this->deleteLater();
                return;
            }

            // 2. 快路径判定 (纯内存只读请求：R-Tree 空间搜桩、单站静态详情、电桩内存状态池、CORS 嗅探)
            if (HttpRouter::is_fast_path(req)) {
                auto res = HttpRouter::instance().dispatch(req);
                bool keep_alive = res.keep_alive();

                std::ostringstream oss;
                oss << res;
                std::string res_bytes = oss.str();

                if (socket_ && socket_->isOpen()) {
                    socket_->write(res_bytes.data(), static_cast<qint64>(res_bytes.size()));
                    // 彻底移除阻塞的 flush()，交由 Qt 事件循环非阻塞按需下发
                }

                if (!keep_alive) {
                    if (socket_) {
                        socket_->disconnectFromHost();
                    }
                    return;
                }

                // Keep-Alive 保持连接：重置解析器并继续推进管道中下一条请求
                parser_ = std::make_unique<http::request_parser<http::string_body>>();
                parser_->body_limit(2 * 1024 * 1024);
                continue;
            }

            // 3. 慢路径/DB操作解耦：异步投递至业务工作线程池，彻底解放 Reactor I/O 事件循环
            is_processing_ = true;
            BusinessThreadPool::instance().submit([self = QPointer<QtHttpSession>(this), req = std::move(req)]() {
                if (!self) return;

                auto res = HttpRouter::instance().dispatch(req);
                bool keep_alive = res.keep_alive();

                std::ostringstream oss;
                oss << res;
                std::string res_bytes = oss.str();

                if (!self) return;
                QMetaObject::invokeMethod(self, [self, res_bytes = std::move(res_bytes), keep_alive]() {
                    if (!self) return;
                    self->on_async_response_ready(std::move(res_bytes), keep_alive);
                }, Qt::QueuedConnection);
            });

            break; // 跳出当前解析循环，等待该请求异步完成并写回后，再按序推进 Keep-Alive 下一条请求
        } else if (bytes_used == 0) {
            // 缓冲区内无足够字节推进解析状态，等待后续 readyRead 信号
            break;
        }
    }
}

void QtHttpSession::on_async_response_ready(std::string res_bytes, bool keep_alive) {
    if (socket_ && socket_->isOpen()) {
        socket_->write(res_bytes.data(), static_cast<qint64>(res_bytes.size()));
    }

    if (!keep_alive) {
        if (socket_) {
            socket_->disconnectFromHost();
        }
        return;
    }

    // Keep-Alive 连接复用：重置解析器并解除处理中标记
    parser_ = std::make_unique<http::request_parser<http::string_body>>();
    parser_->body_limit(2 * 1024 * 1024);
    is_processing_ = false;

    // 唤醒继续处理可能已缓存在 raw_buffer_ 中的后续请求
    process_pipeline();
}

} // namespace ev
