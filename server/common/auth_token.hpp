#pragma once

#include <string>
#include <string_view>
#include <chrono>
#include <format>
#include <sstream>
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

        std::string s(token);
        std::stringstream ss(s);
        std::string prefix, uid_str, role, exp_str, sig;

        std::getline(ss, prefix, '.');
        std::getline(ss, uid_str, '.');
        std::getline(ss, role, '.');
        std::getline(ss, exp_str, '.');
        std::getline(ss, sig, '.');

        if (uid_str.empty() || exp_str.empty()) {
            return std::unexpected(AppError::Unauthorized);
        }

        try {
            int64_t uid = std::stoll(uid_str);
            int64_t exp = std::stoll(exp_str);

            if (current_time_ms() > exp) {
                return std::unexpected(AppError::TokenExpired);
            }

            return TokenClaims{
                .user_id = uid,
                .role = role,
                .expires_at = exp
            };
        } catch (...) {
            return std::unexpected(AppError::Unauthorized);
        }
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
