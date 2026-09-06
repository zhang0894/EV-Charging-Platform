#include "qt_ws_session.hpp"
#include "../memory/state_pool.hpp"
#include "../common/types.hpp"
#include <QCryptographicHash>
#include <QString>
#include <regex>
#include <iostream>

namespace ev {

QtWsSession::QtWsSession(QTcpSocket* socket, QObject* parent)
    : QObject(parent), socket_(socket) {
    if (socket_) {
        socket_->setParent(this);
        connect(socket_, &QTcpSocket::readyRead, this, &QtWsSession::onReadyRead);
        connect(socket_, &QTcpSocket::disconnected, this, &QtWsSession::onDisconnected);
    }
}

QtWsSession::~QtWsSession() {
    is_open_ = false;
    if (socket_) {
        socket_->disconnect(this);
        socket_->deleteLater();
        socket_ = nullptr;
    }
}

bool QtWsSession::is_open() const {
    return is_open_.load() && socket_ != nullptr && socket_->isOpen();
}

void QtWsSession::start(const http::request<http::string_body>& req) {
    perform_handshake(req);
    is_open_ = true;
    route_subscription(req.target());
}

void QtWsSession::perform_handshake(const http::request<http::string_body>& req) {
    std::string key = std::string(req[http::field::sec_websocket_key]);
    if (key.empty()) {
        socket_->disconnectFromHost();
        return;
    }

    // RFC 6455 Sec-WebSocket-Accept 握手签名计算
    QString combined = QString::fromStdString(key) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    QByteArray hash = QCryptographicHash::hash(combined.toUtf8(), QCryptographicHash::Sha1);
    QByteArray accept_key = hash.toBase64();

    QByteArray response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept_key + "\r\n\r\n";

    socket_->write(response);
    socket_->flush();
}

void QtWsSession::route_subscription(std::string_view target) {
    std::string target_str(target);

    // 1. 充电遥测流: /ws/v1/charging/{order_id}
    std::regex charging_ws_regex(R"(^/ws/v1/charging/([^/]+)$)");
    std::cmatch match;
    if (std::regex_match(target_str.c_str(), match, charging_ws_regex)) {
        std::string order_id = match[1].str();
        WsManager::instance().subscribe_charging(order_id, shared_from_this());
    }

    // 2. 站点导航监控流: /ws/v1/stations/{station_id}/monitor
    std::regex station_ws_regex(R"(^/ws/v1/stations/(\d+)/monitor$)");
    if (std::regex_match(target_str.c_str(), match, station_ws_regex)) {
        int64_t sid = std::stoll(match[1].str());
        WsManager::instance().subscribe_station(sid, shared_from_this());

        // 立即向客户端推送初始快照帧
        auto piles = ChargingStatePool::instance().get_piles_by_station(sid);
        auto sum = ChargingStatePool::instance().get_station_pile_summary(sid);

        StationSnapshotFrame snapshot{
            .event = "STATION_SNAPSHOT",
            .station_id = sid,
            .station_name = "充电站实时状态监控",
            .data = StationSnapshotData{
                .total_piles = sum.total_piles,
                .idle_piles = sum.idle_piles,
                .fast_idle_piles = sum.fast_piles_idle,
                .slow_idle_piles = sum.slow_piles_idle,
                .busy_piles = sum.busy_piles,
                .fault_piles = sum.fault_piles,
                .queueing_cars = 0,
                .estimated_wait_minutes = (sum.idle_piles > 0 ? 0 : 15)
            },
            .timestamp = current_time_ms()
        };

        for (const auto& p : piles) {
            snapshot.data.piles.push_back(StationNavPileStatusDTO{
                .pile_id = p.pile_id,
                .type = p.type,
                .status = p.status,
                .power_kw = p.power_kw,
                .current_soc = p.current_soc,
                .est_remaining_mins = (p.status == "CHARGING" ? std::max(5, (100 - p.current_soc) / 2) : 0)
            });
        }

        std::string json_str;
        if (!glz::write_json(snapshot, json_str)) {
            send_json(json_str);
        }
    }

    // 3. 全局告警流: /ws/v1/events
    if (target.starts_with("/ws/v1/events")) {
        WsManager::instance().subscribe_global(shared_from_this());
    }
}

void QtWsSession::send_json(const std::string& json_text) {
    if (!is_open_.load()) return;

    QByteArray frame = build_websocket_frame(json_text, 0x01);

    // 跨线程安全派发至 Socket 所在线程发送
    QMetaObject::invokeMethod(this, [this, frame = std::move(frame)]() {
        if (socket_ && socket_->isOpen()) {
            socket_->write(frame);
        }
    }, Qt::QueuedConnection);
}

void QtWsSession::onReadyRead() {
    if (!socket_) return;
    read_buffer_.append(socket_->readAll());
    parse_incoming_frames();
}

void QtWsSession::onDisconnected() {
    is_open_ = false;
    if (socket_) {
        socket_->disconnect(this);
        socket_->deleteLater();
        socket_ = nullptr;
    }
}

void QtWsSession::parse_incoming_frames() {
    while (read_buffer_.size() >= 2) {
        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(read_buffer_.constData());
        uint8_t b0 = ptr[0];
        uint8_t opcode = (b0 & 0x0F);

        uint8_t b1 = ptr[1];
        bool masked = (b1 & 0x80) != 0;
        uint64_t payload_len = (b1 & 0x7F);

        size_t header_len = 2;
        if (payload_len == 126) {
            if (read_buffer_.size() < 4) return;
            payload_len = (static_cast<uint64_t>(ptr[2]) << 8) | ptr[3];
            header_len += 2;
        } else if (payload_len == 127) {
            if (read_buffer_.size() < 10) return;
            payload_len = 0;
            for (int i = 0; i < 8; ++i) {
                payload_len = (payload_len << 8) | ptr[2 + i];
            }
            header_len += 8;
        }

        if (masked) {
            header_len += 4;
        }

        if (static_cast<size_t>(read_buffer_.size()) < header_len + payload_len) {
            // 数据帧不完整，等待下一次 readyRead
            return;
        }

        // 提取并解码载荷
        QByteArray payload;
        if (payload_len > 0) {
            payload.resize(static_cast<qsizetype>(payload_len));
            const uint8_t* payload_src = ptr + (header_len - (masked ? 4 : 0)) + (masked ? 4 : 0);
            if (masked) {
                const uint8_t* mask = ptr + header_len - 4;
                for (size_t i = 0; i < payload_len; ++i) {
                    payload[static_cast<qsizetype>(i)] = payload_src[i] ^ mask[i % 4];
                }
            } else {
                std::memcpy(payload.data(), payload_src, payload_len);
            }
        }

        // 消费当前帧
        read_buffer_.remove(0, static_cast<qsizetype>(header_len + payload_len));

        // 根据 opcode 处理
        if (opcode == 0x08) {
            // 关闭帧 (Close)
            is_open_ = false;
            socket_->disconnectFromHost();
            return;
        } else if (opcode == 0x09) {
            // Ping 帧 -> 回复 Pong
            socket_->write(build_websocket_frame(std::string_view(payload.constData(), payload.size()), 0x0A));
        } else if (opcode == 0x01) {
            // 文本帧
            handle_text_payload(std::string_view(payload.constData(), payload.size()));
        }
    }
}

void QtWsSession::handle_text_payload(std::string_view payload) {
    if (payload == "PING" || payload == "ping") {
        send_json(R"({"event":"PONG"})");
    }
}

QByteArray QtWsSession::build_websocket_frame(std::string_view payload, uint8_t opcode) {
    QByteArray frame;
    frame.append(static_cast<char>(0x80 | (opcode & 0x0F))); // FIN = 1 + opcode

    size_t len = payload.size();
    if (len <= 125) {
        frame.append(static_cast<char>(len));
    } else if (len <= 65535) {
        frame.append(static_cast<char>(126));
        frame.append(static_cast<char>((len >> 8) & 0xFF));
        frame.append(static_cast<char>(len & 0xFF));
    } else {
        frame.append(static_cast<char>(127));
        for (int i = 7; i >= 0; --i) {
            frame.append(static_cast<char>((len >> (i * 8)) & 0xFF));
        }
    }

    frame.append(payload.data(), static_cast<qsizetype>(payload.size()));
    return frame;
}

} // namespace ev
