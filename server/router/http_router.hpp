#pragma once

#include <boost/beast/http.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <functional>

namespace ev {

namespace http = boost::beast::http;

class HttpRouter {
public:
    static HttpRouter& instance() {
        static HttpRouter router;
        return router;
    }

    http::response<http::string_body> dispatch(const http::request<http::string_body>& req);

    // URL 查询参数解析辅助函数
    static std::unordered_map<std::string, std::string> parse_query_params(std::string_view target);

    // 路径切片与路径变量提取
    static std::string_view extract_path_only(std::string_view target);

private:
    HttpRouter() = default;
};

} // namespace ev
