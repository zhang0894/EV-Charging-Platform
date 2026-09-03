#pragma once

#include "../common/types.hpp"
#include "../common/error.hpp"
#include "../common/models.hpp"
#include "../common/response.hpp"
#include "../db/db_repository.hpp"
#include <glaze/glaze.hpp>

namespace ev {

class UserController {
public:
    static http::response<http::string_body> handle_get_profile(int64_t user_id) {
        auto u_res = DbRepository::instance().get_user_by_id(user_id);
        if (!u_res) {
            return make_error_response(u_res.error());
        }

        auto w_res = DbRepository::instance().get_wallet(user_id);
        int64_t balance_cents = w_res ? w_res->balance_cents : 0;
        int64_t frozen_cents = w_res ? w_res->frozen_cents : 0;

        auto act_res = DbRepository::instance().get_active_order_by_user(user_id);
        bool has_active = act_res.has_value() && act_res.value().has_value();

        UserProfileResponseData data{
            .user_id = u_res->user_id,
            .phone = u_res->phone,
            .nickname = u_res->nickname,
            .avatar_url = u_res->avatar_url,
            .balance = cents_to_yuan(balance_cents),
            .balance_cents = balance_cents,
            .frozen_amount = cents_to_yuan(frozen_cents),
            .frozen_cents = frozen_cents,
            .status = u_res->status,
            .status_desc = (u_res->status == 1 ? "NORMAL" : "FROZEN"),
            .has_active_order = has_active,
            .created_at = u_res->created_at
        };

        return make_success_response(data);
    }

    static http::response<http::string_body> handle_update_profile(int64_t user_id, const http::request<http::string_body>& req) {
        UpdateProfileRequest p_req;
        auto err = glz::read_json(p_req, req.body());
        if (err || p_req.nickname.empty()) {
            return make_error_response(AppError::InvalidJsonPayload, "Missing nickname");
        }

        auto res = DbRepository::instance().update_user_nickname(user_id, p_req.nickname);
        if (!res) {
            return make_error_response(res.error());
        }

        return make_empty_success_response();
    }

    static http::response<http::string_body> handle_change_password(int64_t user_id, const http::request<http::string_body>& req) {
        ChangePasswordRequest cp_req;
        auto err = glz::read_json(cp_req, req.body());
        if (err || cp_req.new_password.empty()) {
            return make_error_response(AppError::InvalidJsonPayload, "Missing old_password or new_password");
        }

        auto u_res = DbRepository::instance().get_user_by_id(user_id);
        if (!u_res) {
            return make_error_response(u_res.error());
        }

        if (u_res->status == 2) {
            return make_error_response(AppError::UserAccountFrozen);
        }

        if (!u_res->password_hash.empty() && u_res->password_hash != cp_req.old_password) {
            return make_error_response(AppError::InvalidCredentials, "Incorrect old password");
        }

        auto upd = DbRepository::instance().update_user_password(user_id, cp_req.new_password);
        if (!upd) {
            return make_error_response(upd.error());
        }

        return make_empty_success_response();
    }

    static http::response<http::string_body> handle_upload_avatar(int64_t user_id, const http::request<http::string_body>& req) {
        UploadAvatarRequest a_req;
        auto err = glz::read_json(a_req, req.body());
        if (err || a_req.image_base64.empty()) {
            return make_error_response(AppError::InvalidJsonPayload, "Missing image_base64");
        }

        std::string avatar_url = std::format("http://localhost:8080/static/avatars/user_{}_{}.png", user_id, current_time_ms());
        auto res = DbRepository::instance().update_user_avatar(user_id, avatar_url);
        if (!res) {
            return make_error_response(res.error());
        }

        struct AvatarResp {
            std::string avatar_url;
        };
        return make_success_response(AvatarResp{.avatar_url = avatar_url});
    }

    static http::response<http::string_body> handle_get_wallet_balance(int64_t user_id) {
        auto w_res = DbRepository::instance().get_wallet(user_id);
        if (!w_res) {
            return make_error_response(w_res.error());
        }

        WalletBalanceResponseData data{
            .user_id = user_id,
            .balance = cents_to_yuan(w_res->balance_cents),
            .balance_cents = w_res->balance_cents,
            .frozen_amount = cents_to_yuan(w_res->frozen_cents),
            .frozen_cents = w_res->frozen_cents,
            .available_amount = cents_to_yuan(w_res->balance_cents - w_res->frozen_cents),
            .currency = "CNY"
        };

        return make_success_response(data);
    }

    static http::response<http::string_body> handle_recharge(int64_t user_id, const http::request<http::string_body>& req) {
        RechargeRequest r_req;
        auto err = glz::read_json(r_req, req.body());
        if (err) {
            return make_error_response(AppError::InvalidJsonPayload);
        }

        int64_t amount_cents = (r_req.amount_cents > 0) ? r_req.amount_cents : yuan_to_cents(r_req.amount);
        if (amount_cents <= 0) {
            return make_error_response(AppError::InvalidAmount, "Recharge amount must be greater than 0");
        }

        // 获取 Idempotency-Key Header
        std::string idem_key;
        auto it = req.find("Idempotency-Key");
        if (it != req.end()) {
            idem_key = std::string(it->value());
        }
        if (idem_key.empty()) {
            idem_key = std::format("REC_{}_{}_{}", user_id, current_time_ms(), amount_cents);
        }

        auto res = DbRepository::instance().recharge_wallet(user_id, amount_cents, idem_key, r_req.remark);
        if (!res) {
            return make_error_response(res.error());
        }

        return make_success_response(*res);
    }

    static http::response<http::string_body> handle_get_transactions(
        int64_t user_id,
        int page,
        int page_size,
        int flow_type
    ) {
        auto res = DbRepository::instance().get_transactions_paged(user_id, page, page_size, flow_type);
        if (!res) {
            return make_error_response(res.error());
        }

        return make_success_response(*res);
    }
};

} // namespace ev
