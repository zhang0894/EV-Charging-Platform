#pragma once

#include "../common/types.hpp"
#include "../common/error.hpp"
#include "../common/models.hpp"
#include "../common/response.hpp"
#include "../db/db_repository.hpp"
#include "../memory/avatar_manager.hpp"
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
        if (req.body().empty()) {
            return make_error_response(AppError::InvalidParameters, "Empty file body");
        }

        constexpr size_t MAX_SIZE = 1024 * 1024; // 1MB 严格限制 (< 1MB)
        if (req.body().size() >= MAX_SIZE) {
            return make_error_response(AppError::PayloadTooLarge, "Avatar file size must be less than 1MB");
        }

        std::string_view raw_body = req.body();
        std::string content_type;
        auto ct_it = req.find(http::field::content_type);
        if (ct_it != req.end()) {
            content_type = std::string(ct_it->value());
        }

        std::string_view file_data = raw_body;

        // 支持 multipart/form-data 提取二进制文件体
        if (content_type.find("multipart/form-data") != std::string::npos) {
            auto b_pos = content_type.find("boundary=");
            if (b_pos != std::string::npos) {
                std::string boundary = "--" + content_type.substr(b_pos + 9);
                if (boundary.back() == '"') boundary.pop_back();
                if (boundary.size() > 2 && boundary[2] == '"') boundary.erase(2, 1);

                auto p1 = raw_body.find(boundary);
                if (p1 != std::string_view::npos) {
                    auto header_end = raw_body.find("\r\n\r\n", p1);
                    if (header_end != std::string_view::npos) {
                        auto next_b = raw_body.find(boundary, header_end + 4);
                        if (next_b != std::string_view::npos) {
                            size_t data_end = next_b;
                            if (data_end >= 2 && raw_body[data_end - 2] == '\r' && raw_body[data_end - 1] == '\n') {
                                data_end -= 2;
                            }
                            file_data = raw_body.substr(header_end + 4, data_end - (header_end + 4));

                            std::string_view part_header = raw_body.substr(p1, header_end - p1);
                            auto part_ct_pos = part_header.find("Content-Type: ");
                            if (part_ct_pos != std::string_view::npos) {
                                auto ct_end = part_header.find("\r\n", part_ct_pos);
                                content_type = std::string(part_header.substr(part_ct_pos + 14, ct_end - (part_ct_pos + 14)));
                            } else {
                                content_type.clear();
                            }
                        }
                    }
                }
            }
        }

        if (file_data.size() >= MAX_SIZE) {
            return make_error_response(AppError::PayloadTooLarge, "Avatar file size must be less than 1MB");
        }

        if (file_data.empty()) {
            return make_error_response(AppError::InvalidParameters, "Empty file content");
        }

        if (content_type.empty() || content_type.find("multipart") != std::string::npos || content_type.find("octet-stream") != std::string::npos) {
            content_type = AvatarManager::detect_image_content_type(file_data);
        }

        auto save_res = AvatarManager::instance().save_avatar(user_id, content_type, file_data);
        if (!save_res) {
            return make_error_response(save_res.error());
        }

        return make_success_response(*save_res);
    }

    static http::response<http::string_body> handle_get_avatar(int64_t user_id, const http::request<http::string_body>& req) {
        auto avatar_opt = AvatarManager::instance().get_avatar(user_id);
        if (!avatar_opt) {
            return make_error_response(AppError::AvatarNotFound, "Avatar not found");
        }

        const auto& avatar = *avatar_opt;

        // HTTP 304 协商缓存 (If-None-Match)
        auto inm_it = req.find(http::field::if_none_match);
        if (inm_it != req.end() && inm_it->value() == avatar.etag) {
            http::response<http::string_body> res{http::status::not_modified, req.version()};
            res.set(http::field::server, "Modern-Cpp23-Charging-Server");
            res.set(http::field::etag, avatar.etag);
            res.set(http::field::cache_control, "public, max-age=3600");
            res.set(http::field::access_control_allow_origin, "*");
            res.keep_alive(req.keep_alive());
            return res;
        }

        // 直接传输真实头像二进制图片流
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, "Modern-Cpp23-Charging-Server");
        res.set(http::field::content_type, avatar.content_type);
        res.set(http::field::etag, avatar.etag);
        res.set(http::field::cache_control, "public, max-age=3600");
        res.set(http::field::access_control_allow_origin, "*");
        res.body() = avatar.data;
        res.prepare_payload();
        res.keep_alive(req.keep_alive());
        return res;
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
