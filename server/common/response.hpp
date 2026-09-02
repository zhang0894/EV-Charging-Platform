#pragma once

#include "types.hpp"
#include "error.hpp"
#include <boost/beast/http.hpp>
#include <glaze/glaze.hpp>
#include <string>
#include <string_view>
#include <format>

namespace ev {

namespace http = boost::beast::http;

template <typename T>
struct ApiResponse {
    int code{0};
    std::string msg{"success"};
    T data{};
    int64_t timestamp{0};
};

template <>
struct ApiResponse<std::nullptr_t> {
    int code{0};
    std::string msg{"success"};
    std::nullptr_t data{nullptr};
    int64_t timestamp{0};
};

// 基础 HTTP JSON 响应组装函数
inline http::response<http::string_body> make_http_response(
    http::status status,
    std::string_view body,
    unsigned int version = 11,
    bool keep_alive = true
) {
    http::response<http::string_body> res{status, version};
    res.set(http::field::server, "Modern-Cpp23-Charging-Server");
    res.set(http::field::content_type, "application/json; charset=utf-8");
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_headers, "Content-Type, Authorization, Idempotency-Key");
    res.set(http::field::access_control_allow_methods, "GET, POST, PUT, DELETE, OPTIONS");
    res.keep_alive(keep_alive);
    res.body() = body;
    res.prepare_payload();
    return res;
}

// 泛型业务成功响应
template <typename T>
http::response<http::string_body> make_success_response(
    const T& data,
    unsigned int version = 11,
    bool keep_alive = true
) {
    ApiResponse<T> resp{
        .code = 0,
        .msg = "success",
        .data = data,
        .timestamp = current_time_ms()
    };

    std::string json_str;
    auto err = glz::write_json(resp, json_str);
    if (err) {
        std::string fallback = std::format(
            R"({{"code":0,"msg":"success","data":{{}},"timestamp":{}}})",
            current_time_ms()
        );
        return make_http_response(http::status::ok, fallback, version, keep_alive);
    }

    return make_http_response(http::status::ok, json_str, version, keep_alive);
}

// 空数据成功响应
inline http::response<http::string_body> make_empty_success_response(
    unsigned int version = 11,
    bool keep_alive = true
) {
    std::string body = std::format(
        R"({{"code":0,"msg":"success","data":{{}},"timestamp":{}}})",
        current_time_ms()
    );
    return make_http_response(http::status::ok, body, version, keep_alive);
}

// 业务错误响应
inline http::response<http::string_body> make_error_response(
    AppError err,
    std::string_view custom_msg = "",
    unsigned int version = 11,
    bool keep_alive = true
) {
    unsigned int http_status_code = http_status_for_error(err);
    std::string_view msg = custom_msg.empty() ? error_message(err) : custom_msg;

    std::string body = std::format(
        R"({{"code":{},"msg":"{}","data":null,"timestamp":{}}})",
        static_cast<int32_t>(err),
        msg,
        current_time_ms()
    );

    return make_http_response(
        static_cast<http::status>(http_status_code),
        body,
        version,
        keep_alive
    );
}

} // namespace ev
