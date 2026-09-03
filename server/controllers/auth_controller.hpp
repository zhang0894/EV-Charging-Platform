#pragma once

#include "../common/types.hpp"
#include "../common/error.hpp"
#include "../common/models.hpp"
#include "../common/auth_token.hpp"
#include "../common/response.hpp"
#include "../db/db_repository.hpp"
#include <glaze/glaze.hpp>

namespace ev {

class AuthController {
public:
    static http::response<http::string_body> handle_user_login(const http::request<http::string_body>& req) {
        LoginRequest login_req;
        auto err = glz::read_json(login_req, req.body());
        if (err || login_req.phone.empty()) {
            return make_error_response(AppError::InvalidJsonPayload, "Invalid login JSON payload or missing phone");
        }

        if (login_req.phone.size() != 11) {
            return make_error_response(AppError::InvalidPhoneFormat);
        }

        auto res = DbRepository::instance().get_user_by_phone(login_req.phone);
        if (!res) {
            // 用户不存在，返回 10001 业务码而非自动注册
            return make_error_response(AppError::UserNotFound, "User not found");
        }

        auto user = *res;
        if (user.status == 2) {
            return make_error_response(AppError::UserAccountFrozen);
        }

        auto wallet = DbRepository::instance().get_wallet(user.user_id);
        int64_t balance_cents = wallet ? wallet->balance_cents : 0;

        std::string access_token = AuthTokenManager::generate_token(user.user_id, user.role, 7200);
        std::string refresh_token = AuthTokenManager::generate_token(user.user_id, user.role, 86400 * 7);

        AuthResponseData resp_data{
            .user_id = user.user_id,
            .phone = user.phone,
            .nickname = user.nickname,
            .avatar_url = user.avatar_url,
            .balance = cents_to_yuan(balance_cents),
            .balance_cents = balance_cents,
            .is_new_user = false,
            .access_token = access_token,
            .refresh_token = refresh_token,
            .role = user.role,
            .expires_in = 7200
        };

        return make_success_response(resp_data);
    }

    static http::response<http::string_body> handle_user_register(const http::request<http::string_body>& req) {
        RegisterRequest reg_req;
        auto err = glz::read_json(reg_req, req.body());
        if (err || reg_req.phone.empty()) {
            return make_error_response(AppError::InvalidJsonPayload, "Invalid register JSON payload or missing phone");
        }

        if (reg_req.phone.size() != 11) {
            return make_error_response(AppError::InvalidPhoneFormat);
        }

        auto existing = DbRepository::instance().get_user_by_phone(reg_req.phone);
        if (existing) {
            return make_error_response(AppError::UserAlreadyExists, "Phone number already registered");
        }

        auto res = DbRepository::instance().create_user(reg_req.phone, reg_req.password, reg_req.nickname, "user");
        if (!res) {
            return make_error_response(res.error());
        }

        auto user = *res;
        std::string access_token = AuthTokenManager::generate_token(user.user_id, "user", 7200);
        std::string refresh_token = AuthTokenManager::generate_token(user.user_id, "user", 86400 * 7);

        AuthResponseData resp_data{
            .user_id = user.user_id,
            .phone = user.phone,
            .nickname = user.nickname,
            .avatar_url = user.avatar_url,
            .balance = 0.0,
            .balance_cents = 0,
            .is_new_user = true,
            .access_token = access_token,
            .refresh_token = refresh_token,
            .role = "user",
            .expires_in = 7200
        };

        return make_success_response(resp_data);
    }

    static http::response<http::string_body> handle_user_password_login(const http::request<http::string_body>& req) {
        PasswordLoginRequest p_req;
        auto err = glz::read_json(p_req, req.body());
        if (err || p_req.phone.empty() || p_req.password.empty()) {
            return make_error_response(AppError::InvalidJsonPayload, "Missing phone or password");
        }

        if (p_req.phone.size() != 11) {
            return make_error_response(AppError::InvalidPhoneFormat);
        }

        auto res = DbRepository::instance().get_user_by_phone(p_req.phone);
        if (!res) {
            return make_error_response(AppError::UserNotFound, "User not found");
        }

        auto user = *res;
        if (user.status == 2) {
            return make_error_response(AppError::UserAccountFrozen);
        }

        if (user.password_hash != p_req.password) {
            return make_error_response(AppError::InvalidCredentials, "Incorrect password");
        }

        auto wallet = DbRepository::instance().get_wallet(user.user_id);
        int64_t balance_cents = wallet ? wallet->balance_cents : 0;

        std::string access_token = AuthTokenManager::generate_token(user.user_id, user.role, 7200);
        std::string refresh_token = AuthTokenManager::generate_token(user.user_id, user.role, 86400 * 7);

        AuthResponseData resp_data{
            .user_id = user.user_id,
            .phone = user.phone,
            .nickname = user.nickname,
            .avatar_url = user.avatar_url,
            .balance = cents_to_yuan(balance_cents),
            .balance_cents = balance_cents,
            .is_new_user = false,
            .access_token = access_token,
            .refresh_token = refresh_token,
            .role = user.role,
            .expires_in = 7200
        };

        return make_success_response(resp_data);
    }

    static http::response<http::string_body> handle_change_password(const http::request<http::string_body>& req) {
        ChangePasswordRequest cp_req;
        auto err = glz::read_json(cp_req, req.body());
        if (err || cp_req.new_password.empty() || cp_req.phone.empty()) {
            return make_error_response(AppError::InvalidJsonPayload, "Missing phone, old_password, or new_password");
        }

        auto u_res = DbRepository::instance().get_user_by_phone(cp_req.phone);
        if (!u_res) {
            return make_error_response(AppError::UserNotFound, "User not found");
        }

        if (u_res->status == 2) {
            return make_error_response(AppError::UserAccountFrozen);
        }

        if (!u_res->password_hash.empty() && u_res->password_hash != cp_req.old_password) {
            return make_error_response(AppError::InvalidCredentials, "Incorrect old password");
        }

        auto upd = DbRepository::instance().update_user_password(u_res->user_id, cp_req.new_password);
        if (!upd) {
            return make_error_response(upd.error());
        }

        return make_empty_success_response();
    }

    static http::response<http::string_body> handle_admin_login(const http::request<http::string_body>& req) {
        LoginRequest login_req;
        auto err = glz::read_json(login_req, req.body());
        if (err) {
            return make_error_response(AppError::InvalidJsonPayload);
        }

        std::string account = !login_req.account.empty() ? login_req.account : login_req.phone;
        if (account.empty()) {
            return make_error_response(AppError::InvalidParameters, "Account cannot be empty");
        }

        auto u_res = DbRepository::instance().get_user_by_account(account);
        if (!u_res) {
            return make_error_response(AppError::InvalidCredentials, "Admin account not found");
        }

        auto user = *u_res;
        if (user.role != "admin") {
            return make_error_response(AppError::PermissionDenied, "Account is not an administrator");
        }

        if (!user.password_hash.empty() && user.password_hash != login_req.password) {
            return make_error_response(AppError::InvalidCredentials, "Incorrect admin password");
        }

        std::string access_token = AuthTokenManager::generate_token(user.user_id, "admin", 7200);
        std::string refresh_token = AuthTokenManager::generate_token(user.user_id, "admin", 86400 * 7);

        AuthResponseData resp_data{
            .user_id = user.user_id,
            .admin_id = user.user_id,
            .username = user.nickname,
            .phone = user.phone,
            .nickname = user.nickname,
            .avatar_url = user.avatar_url,
            .access_token = access_token,
            .refresh_token = refresh_token,
            .role = "admin",
            .expires_in = 7200
        };

        return make_success_response(resp_data);
    }

    static http::response<http::string_body> handle_refresh_token(const http::request<http::string_body>& req) {
        RefreshTokenRequest ref_req;
        auto err = glz::read_json(ref_req, req.body());
        if (err || ref_req.refresh_token.empty()) {
            return make_error_response(AppError::InvalidJsonPayload, "Missing refresh_token");
        }

        auto claims_res = AuthTokenManager::verify_token(ref_req.refresh_token);
        if (!claims_res) {
            return make_error_response(claims_res.error());
        }

        std::string new_access = AuthTokenManager::generate_token(claims_res->user_id, claims_res->role, 7200);
        std::string new_refresh = AuthTokenManager::generate_token(claims_res->user_id, claims_res->role, 86400 * 7);

        AuthResponseData resp_data{
            .user_id = claims_res->user_id,
            .access_token = new_access,
            .refresh_token = new_refresh,
            .role = claims_res->role,
            .expires_in = 7200
        };

        return make_success_response(resp_data);
    }
};

} // namespace ev
