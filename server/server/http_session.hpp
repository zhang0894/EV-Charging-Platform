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

            http::request_parser<http::string_body> parser;
            parser.body_limit(2 * 1024 * 1024); // 2MB，允许业务层精确校验 < 1MB 并返回规范的 HTTP 413
            auto [ec, bytes_read] = co_await http::async_read(stream, buffer, parser, net::as_tuple(net::use_awaitable));

            if (ec == http::error::end_of_stream) {
                break;
            }
            if (ec) {
                break;
            }

            auto req = parser.release();

            // 判断是否为 WebSocket 升级请求
            if (beast::websocket::is_upgrade(req)) {
                stream.expires_never();
                auto ws = std::make_shared<WsSession>(stream.release_socket());
                co_await ws->run(std::move(req));
                break;
            }

            // Scheme A: 高性能多线程协程分发 (直接在 Session Strand 上运行)
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
