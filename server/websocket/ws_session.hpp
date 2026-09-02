#pragma once

#include "ws_manager.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <memory>
#include <string>
#include <queue>
#include <mutex>

namespace ev {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class WsSession : public IWebSocketSession, public std::enable_shared_from_this<WsSession> {
public:
    explicit WsSession(tcp::socket&& socket);
    ~WsSession() override = default;

    net::awaitable<void> run(beast::http::request<beast::http::string_body> req);

    void send_json(const std::string& json_text) override;
    bool is_open() const override;

private:
    net::awaitable<void> write_loop();
    net::awaitable<void> read_loop();

    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
    std::queue<std::string> write_queue_;
    std::mutex queue_mutex_;
    std::atomic<bool> is_open_{false};
};

} // namespace ev
