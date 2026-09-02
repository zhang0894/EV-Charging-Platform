#pragma once

#include "../router/http_router.hpp"
#include "../websocket/ws_session.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/as_tuple.hpp>
#include <memory>
#include <iostream>

namespace ev {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

inline net::awaitable<void> handle_session(tcp::socket socket) {
    beast::tcp_stream stream(std::move(socket));
    beast::flat_buffer buffer;

    try {
        for (;;) {
            // 设置 30s 请求读取超时
            stream.expires_after(std::chrono::seconds(30));

            http::request<http::string_body> req;
            auto [ec, bytes_read] = co_await http::async_read(stream, buffer, req, net::as_tuple(net::use_awaitable));

            if (ec == http::error::end_of_stream) {
                break;
            }
            if (ec) {
                break;
            }

            // 判断是否为 WebSocket 升级请求
            if (beast::websocket::is_upgrade(req)) {
                stream.expires_never();
                auto ws = std::make_shared<WsSession>(stream.release_socket());
                co_await ws->run(std::move(req));
                break;
            }

            // HTTP RESTful 请求分发
            auto res = HttpRouter::instance().dispatch(req);
            bool keep_alive = res.keep_alive();

            co_await http::async_write(stream, res, net::use_awaitable);

            if (!keep_alive) {
                break;
            }
        }
    } catch (const std::exception& e) {
        // 连接正常断开或异常终止
    }

    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_send, ec);
}

} // namespace ev
