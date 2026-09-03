#pragma once

#include <string>
#include <string_view>
#include <chrono>
#include <format>
#include <charconv>
#include "types.hpp"
#include "error.hpp"

namespace ev {

struct TokenClaims {
    int64_t user_id{0};
    std::string role{"user"};
    int64_t expires_at{0};
};

class AuthTokenManager {
public:
    static std::string generate_token(int64_t user_id, std::string_view role, int ttl_seconds = 7200) {
        int64_t expires = current_time_ms() + (static_cast<int64_t>(ttl_seconds) * 1000LL);
        // 格式: TOKEN.<user_id>.<role>.<expires_at>.<mock_signature>
        return std::format("EV_TOKEN.{}.{}.{}.SIG_{}", user_id, role, expires, (user_id * 31 + expires % 9973));
    }

    static Result<TokenClaims> verify_token(std::string_view token) {
        if (token.empty() || !token.starts_with("EV_TOKEN.")) {
            return std::unexpected(AppError::Unauthorized);
        }

        // EV_TOKEN.<user_id>.<role>.<expires_at>.<sig>
        std::string_view rem = token.substr(9); // skip "EV_TOKEN."
        auto p1 = rem.find('.');
        if (p1 == std::string_view::npos) return std::unexpected(AppError::Unauthorized);
        std::string_view uid_str = rem.substr(0, p1);

        rem = rem.substr(p1 + 1);
        auto p2 = rem.find('.');
        if (p2 == std::string_view::npos) return std::unexpected(AppError::Unauthorized);
        std::string_view role = rem.substr(0, p2);

        rem = rem.substr(p2 + 1);
        auto p3 = rem.find('.');
        if (p3 == std::string_view::npos) return std::unexpected(AppError::Unauthorized);
        std::string_view exp_str = rem.substr(0, p3);

        int64_t uid = 0;
        int64_t exp = 0;
        auto res1 = std::from_chars(uid_str.data(), uid_str.data() + uid_str.size(), uid);
        auto res2 = std::from_chars(exp_str.data(), exp_str.data() + exp_str.size(), exp);
        if (res1.ec != std::errc() || res2.ec != std::errc()) {
            return std::unexpected(AppError::Unauthorized);
        }

        if (current_time_ms() > exp) {
            return std::unexpected(AppError::TokenExpired);
        }

        return TokenClaims{
            .user_id = uid,
            .role = std::string(role),
            .expires_at = exp
        };
    }

    static Result<TokenClaims> extract_and_verify(std::string_view auth_header) {
        if (auth_header.empty()) {
            return std::unexpected(AppError::Unauthorized);
        }

        std::string_view token = auth_header;
        if (auth_header.starts_with("Bearer ")) {
            token = auth_header.substr(7);
        }

        return verify_token(token);
    }
};

} // namespace ev
