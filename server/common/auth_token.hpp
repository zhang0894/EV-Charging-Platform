#pragma once

#include <string>
#include <string_view>
#include <chrono>
#include <format>
#include <charconv>
#include <shared_mutex>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include "types.hpp"
#include "error.hpp"

namespace ev {

inline constexpr int ACCESS_TOKEN_TTL_SECONDS = 120; // 2 分钟 (120 秒)
inline constexpr int REFRESH_TOKEN_TTL_SECONDS = 86400 * 7; // 7 天 (604800 秒)

struct TokenClaims {
    int64_t user_id{0};
    std::string role{"user"};
    int64_t expires_at{0};
    int64_t issued_at{0};
};

class AuthTokenManager {
private:
    static inline std::shared_mutex s_auth_mutex;
    static inline std::unordered_set<int64_t> s_frozen_users;
    static inline std::unordered_map<int64_t, int64_t> s_user_revoked_at; // user_id -> revoked_at timestamp (ms)

public:
    static void init_frozen_users(const std::vector<std::pair<int64_t, int64_t>>& frozen_users) {
        std::unique_lock lock(s_auth_mutex);
        s_frozen_users.clear();
        for (const auto& [uid, revoked_at] : frozen_users) {
            s_frozen_users.insert(uid);
            s_user_revoked_at[uid] = (revoked_at > 0 ? revoked_at : current_time_ms());
        }
    }

    static void set_user_frozen(int64_t user_id, bool frozen) {
        std::unique_lock lock(s_auth_mutex);
        if (frozen) {
            s_frozen_users.insert(user_id);
            s_user_revoked_at[user_id] = current_time_ms();
        } else {
            s_frozen_users.erase(user_id);
        }
    }

    static void set_user_revoked_at(int64_t user_id, int64_t revoked_at) {
        std::unique_lock lock(s_auth_mutex);
        s_user_revoked_at[user_id] = (revoked_at > 0 ? revoked_at : current_time_ms());
    }

    static void revoke_user_tokens(int64_t user_id) {
        std::unique_lock lock(s_auth_mutex);
        s_user_revoked_at[user_id] = current_time_ms();
    }

    static bool is_user_frozen(int64_t user_id) {
        std::shared_lock lock(s_auth_mutex);
        return s_frozen_users.contains(user_id);
    }

    static bool is_token_revoked(int64_t user_id, int64_t issued_at) {
        std::shared_lock lock(s_auth_mutex);
        auto it = s_user_revoked_at.find(user_id);
        if (it != s_user_revoked_at.end()) {
            if (issued_at <= it->second) {
                return true;
            }
        }
        return false;
    }

    static std::string generate_token(int64_t user_id, std::string_view role, int ttl_seconds = ACCESS_TOKEN_TTL_SECONDS) {
        int64_t now = current_time_ms();
        int64_t expires = now + (static_cast<int64_t>(ttl_seconds) * 1000LL);
        // 格式: EV_TOKEN.<user_id>.<role>.<expires_at>.<issued_at>.<mock_signature>
        return std::format("EV_TOKEN.{}.{}.{}.{}.SIG_{}", user_id, role, expires, now, (user_id * 31 + expires % 9973));
    }

    static Result<TokenClaims> verify_token(std::string_view token) {
        if (token.empty() || !token.starts_with("EV_TOKEN.")) {
            return std::unexpected(AppError::Unauthorized);
        }

        // EV_TOKEN.<user_id>.<role>.<expires_at>.<issued_at>.<sig> 或旧版 4 段: EV_TOKEN.<user_id>.<role>.<expires_at>.<sig>
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

        rem = rem.substr(p3 + 1);
        int64_t issued = 0;
        auto p4 = rem.find('.');
        if (p4 != std::string_view::npos) {
            std::string_view iss_str = rem.substr(0, p4);
            auto res_iss = std::from_chars(iss_str.data(), iss_str.data() + iss_str.size(), issued);
            if (res_iss.ec != std::errc()) {
                issued = 0;
            }
        }

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

        if (is_user_frozen(uid)) {
            return std::unexpected(AppError::UserAccountFrozen);
        }

        if (is_token_revoked(uid, issued)) {
            return std::unexpected(AppError::Unauthorized);
        }

        return TokenClaims{
            .user_id = uid,
            .role = std::string(role),
            .expires_at = exp,
            .issued_at = issued
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
