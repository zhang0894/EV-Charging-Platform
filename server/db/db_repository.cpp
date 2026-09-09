#include "db_repository.hpp"
#include "async_flow_persister.hpp"
#include "../cache/redis_cache.hpp"
#include "../memory/station_price_manager.hpp"
#include "../memory/state_pool.hpp"
#include "../memory/station_status_manager.hpp"
#include "../data/static_stations.hpp"
#include <format>
#include <iostream>
#include <sstream>
#include <unordered_set>

namespace ev {

namespace {
std::string sql_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        if (c == '\'') out.push_back('\'');
        out.push_back(c);
    }
    return out;
}
} // namespace

DbRepository& DbRepository::instance() {
    static DbRepository repo;
    return repo;
}

// ==========================================
// 1. 用户与认证
// ==========================================

Result<UserModel> DbRepository::get_user_by_id(int64_t user_id) {
    std::string cache_key = std::format("cache:user:model:{}", user_id);
    auto cached = RedisCache::instance().get_json<UserModel>(cache_key);
    if (cached) return *cached;

    auto conn = DbPool::instance().acquire_reader();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    conn->prepare("stmt_get_user_by_id", "SELECT user_id, phone, password_hash, nickname, avatar_url, role, status, created_at, updated_at FROM users WHERE user_id = $1;", 1);
    std::string uid_str = std::to_string(user_id);
    const char* params[1] = { uid_str.c_str() };
    PgResultGuard res(conn->exec_prepared("stmt_get_user_by_id", 1, params));

    if (!res.is_ok() || res.rows() == 0) {
        return std::unexpected(AppError::UserNotFound);
    }

    UserModel user{
        .user_id = std::stoll(res.value(0, 0)),
        .phone = res.value(0, 1),
        .password_hash = res.value(0, 2),
        .nickname = res.value(0, 3),
        .avatar_url = res.value(0, 4),
        .role = res.value(0, 5),
        .status = std::stoi(res.value(0, 6)),
        .created_at = std::stoll(res.value(0, 7)),
        .updated_at = std::stoll(res.value(0, 8))
    };
    RedisCache::instance().set_json(cache_key, user, 300);
    return user;
}

Result<UserModel> DbRepository::get_user_by_phone(std::string_view phone) {
    auto conn = DbPool::instance().acquire_reader();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    std::string sql = std::format("SELECT user_id, phone, password_hash, nickname, avatar_url, role, status, created_at, updated_at FROM users WHERE phone = '{}';", phone);
    PgResultGuard res(conn->exec(sql.c_str()));
    if (!res.is_ok() || res.rows() == 0) {
        return std::unexpected(AppError::UserNotFound);
    }

    UserModel user;
    user.user_id = std::stoll(res.value(0, 0));
    user.phone = res.value(0, 1);
    user.password_hash = res.value(0, 2);
    user.nickname = res.value(0, 3);
    user.avatar_url = res.value(0, 4);
    user.role = res.value(0, 5);
    user.status = std::stoi(res.value(0, 6));
    user.created_at = std::stoll(res.value(0, 7));
    user.updated_at = std::stoll(res.value(0, 8));

    return user;
}

Result<UserModel> DbRepository::get_user_by_account(std::string_view account) {
    auto conn = DbPool::instance().acquire_reader();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    std::string sql = std::format("SELECT user_id, phone, password_hash, nickname, avatar_url, role, status, created_at, updated_at FROM users WHERE phone = '{}' OR nickname = '{}';", account, account);
    PgResultGuard res(conn->exec(sql.c_str()));
    if (!res.is_ok() || res.rows() == 0) {
        return std::unexpected(AppError::UserNotFound);
    }

    UserModel user;
    user.user_id = std::stoll(res.value(0, 0));
    user.phone = res.value(0, 1);
    user.password_hash = res.value(0, 2);
    user.nickname = res.value(0, 3);
    user.avatar_url = res.value(0, 4);
    user.role = res.value(0, 5);
    user.status = std::stoi(res.value(0, 6));
    user.created_at = std::stoll(res.value(0, 7));
    user.updated_at = std::stoll(res.value(0, 8));

    return user;
}

Result<std::pair<UserModel, bool>> DbRepository::get_or_create_user_passwordless(std::string_view phone) {
    if (phone.size() != 11) {
        return std::unexpected(AppError::InvalidPhoneFormat);
    }

    auto existing = get_user_by_phone(phone);
    if (existing) {
        return std::make_pair(*existing, false);
    }

    // 不存在则创建新用户
    std::string suffix = std::string(phone.substr(7));
    std::string default_nickname = "用户" + suffix;
    int64_t now = current_time_ms();

    return DbPool::instance().with_transaction([&](DbConnection& conn) -> Result<std::pair<UserModel, bool>> {
        std::string insert_user_sql = std::format(
            "INSERT INTO users (phone, password_hash, nickname, avatar_url, role, status, created_at, updated_at) "
            "VALUES ('{}', '', '{}', 'http://localhost:8080/static/avatars/default.png', 'user', 1, {}, {}) RETURNING user_id;",
            phone, default_nickname, now, now
        );

        PgResultGuard res(conn.exec(insert_user_sql.c_str()));
        if (!res.is_ok() || res.rows() == 0) {
            return std::unexpected(AppError::DatabaseError);
        }

        int64_t new_uid = std::stoll(res.value(0, 0));

        // 关联初始化钱包表
        std::string insert_wallet_sql = std::format(
            "INSERT INTO user_wallets (user_id, balance_cents, frozen_cents, status, updated_at) VALUES ({}, 0, 0, 1, {});",
            new_uid, now
        );

        PgResultGuard w_res(conn.exec(insert_wallet_sql.c_str()));
        if (!w_res.is_ok()) {
            return std::unexpected(AppError::DatabaseError);
        }

        UserModel user{
            .user_id = new_uid,
            .phone = std::string(phone),
            .password_hash = "",
            .nickname = default_nickname,
            .avatar_url = "http://localhost:8080/static/avatars/default.png",
            .role = "user",
            .status = 1,
            .created_at = now,
            .updated_at = now
        };

        return std::make_pair(user, true);
    });
}

Result<UserModel> DbRepository::create_user(
    std::string_view phone,
    std::string_view password,
    std::string_view nickname,
    std::string_view role
) {
    if (phone.size() != 11) {
        return std::unexpected(AppError::InvalidPhoneFormat);
    }

    auto existing = get_user_by_phone(phone);
    if (existing) {
        return std::unexpected(AppError::UserAlreadyExists);
    }

    int64_t now = current_time_ms();
    std::string nick = nickname.empty() ? std::format("用户{}", phone.substr(7)) : std::string(nickname);

    return DbPool::instance().with_transaction([&](DbConnection& conn) -> Result<UserModel> {
        std::string sql = std::format(
            "INSERT INTO users (phone, password_hash, nickname, avatar_url, role, status, created_at, updated_at) "
            "VALUES ('{}', '{}', '{}', 'http://localhost:8080/static/avatars/default.png', '{}', 1, {}, {}) RETURNING user_id;",
            phone, password, nick, role, now, now
        );

        PgResultGuard res(conn.exec(sql.c_str()));
        if (!res.is_ok() || res.rows() == 0) {
            return std::unexpected(AppError::DatabaseError);
        }

        int64_t new_uid = std::stoll(res.value(0, 0));

        std::string w_sql = std::format(
            "INSERT INTO user_wallets (user_id, balance_cents, frozen_cents, status, updated_at) VALUES ({}, 0, 0, 1, {});",
            new_uid, now
        );
        PgResultGuard w_res(conn.exec(w_sql.c_str()));
        if (!w_res.is_ok()) return std::unexpected(AppError::DatabaseError);

        UserModel user{
            .user_id = new_uid,
            .phone = std::string(phone),
            .password_hash = std::string(password),
            .nickname = nick,
            .avatar_url = "http://localhost:8080/static/avatars/default.png",
            .role = std::string(role),
            .status = 1,
            .created_at = now,
            .updated_at = now
        };
        return user;
    });
}

Result<void> DbRepository::update_user_password(int64_t user_id, std::string_view new_password) {
    auto conn = DbPool::instance().acquire_writer();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    int64_t now = current_time_ms();
    std::string sql = std::format("UPDATE users SET password_hash = '{}', updated_at = {} WHERE user_id = {};", new_password, now, user_id);
    PgResultGuard res(conn->exec(sql.c_str()));
    if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);

    RedisCache::instance().del(std::format("cache:user:model:{}", user_id));
    return {};
}

Result<void> DbRepository::update_user_nickname(int64_t user_id, std::string_view nickname) {
    auto conn = DbPool::instance().acquire_writer();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    int64_t now = current_time_ms();
    conn->prepare("stmt_update_user_nickname", "UPDATE users SET nickname = $1, updated_at = $2 WHERE user_id = $3;", 3);
    std::string nick_str(nickname);
    std::string now_str = std::to_string(now);
    std::string uid_str = std::to_string(user_id);
    const char* params[3] = { nick_str.c_str(), now_str.c_str(), uid_str.c_str() };
    PgResultGuard res(conn->exec_prepared("stmt_update_user_nickname", 3, params));
    if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);

    RedisCache::instance().del(std::format("cache:user:model:{}", user_id));
    return {};
}

Result<std::string> DbRepository::update_user_avatar(int64_t user_id, std::string_view avatar_url) {
    auto conn = DbPool::instance().acquire_writer();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    int64_t now = current_time_ms();
    conn->prepare("stmt_update_user_avatar", "UPDATE users SET avatar_url = $1, updated_at = $2 WHERE user_id = $3;", 3);
    std::string url_str(avatar_url);
    std::string now_str = std::to_string(now);
    std::string uid_str = std::to_string(user_id);
    const char* params[3] = { url_str.c_str(), now_str.c_str(), uid_str.c_str() };
    PgResultGuard res(conn->exec_prepared("stmt_update_user_avatar", 3, params));
    if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);

    RedisCache::instance().del(std::format("cache:user:model:{}", user_id));
    return std::string(avatar_url);
}

Result<void> DbRepository::save_user_avatar(int64_t user_id, std::string_view content_type, std::string_view binary_data) {
    auto conn = DbPool::instance().acquire_writer();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    int64_t now = current_time_ms();
    std::string uid_str = std::to_string(user_id);
    std::string sz_str = std::to_string(binary_data.size());
    std::string now_str = std::to_string(now);
    std::string ct_str = content_type.empty() ? "image/png" : std::string(content_type);

    const char* paramValues[5] = {
        uid_str.c_str(),
        ct_str.c_str(),
        sz_str.c_str(),
        binary_data.data(),
        now_str.c_str()
    };
    int paramLengths[5] = {
        static_cast<int>(uid_str.size()),
        static_cast<int>(ct_str.size()),
        static_cast<int>(sz_str.size()),
        static_cast<int>(binary_data.size()),
        static_cast<int>(now_str.size())
    };
    int paramFormats[5] = { 0, 0, 0, 1, 0 }; // 第4个参数 ($4) 为原生二进制格式

    static const char* sql =
        "INSERT INTO user_avatars (user_id, content_type, file_size, avatar_data, updated_at) "
        "VALUES ($1, $2, $3, $4::bytea, $5) "
        "ON CONFLICT (user_id) DO UPDATE SET "
        "content_type = EXCLUDED.content_type, "
        "file_size = EXCLUDED.file_size, "
        "avatar_data = EXCLUDED.avatar_data, "
        "updated_at = EXCLUDED.updated_at;";

    PgResultGuard res(conn->exec_params(
        sql,
        5,
        nullptr,
        paramValues,
        paramLengths,
        paramFormats,
        0
    ));

    if (!res.is_ok()) {
        std::cerr << "[DbRepository] save_user_avatar failed: " << conn->last_error() << "\n";
        return std::unexpected(AppError::DatabaseError);
    }
    return {};
}

Result<std::optional<AvatarModel>> DbRepository::get_user_avatar(int64_t user_id) {
    auto conn = DbPool::instance().acquire_reader();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    std::string uid_str = std::to_string(user_id);
    const char* paramValues[1] = { uid_str.c_str() };
    int paramLengths[1] = { static_cast<int>(uid_str.size()) };
    int paramFormats[1] = { 0 };

    static const char* sql = "SELECT content_type, file_size, avatar_data, updated_at FROM user_avatars WHERE user_id = $1;";

    PgResultGuard res(conn->exec_params(
        sql,
        1,
        nullptr,
        paramValues,
        paramLengths,
        paramFormats,
        0
    ));

    if (!res.is_ok()) {
        std::cerr << "[DbRepository] get_user_avatar failed: " << conn->last_error() << "\n";
        return std::unexpected(AppError::DatabaseError);
    }

    if (res.rows() == 0) {
        return std::nullopt;
    }

    std::string ct = res.value(0, 0);
    int sz = std::stoi(res.value(0, 1));
    int64_t updated_at = 0;
    try {
        updated_at = std::stoll(res.value(0, 3));
    } catch (...) {
        updated_at = current_time_ms();
    }

    // 解码 bytea 二进制数据
    std::string binary_bytes;
    const char* raw_val = res.value(0, 2);
    if (raw_val) {
        size_t to_len = 0;
        unsigned char* unescaped = PQunescapeBytea(reinterpret_cast<const unsigned char*>(raw_val), &to_len);
        if (unescaped) {
            binary_bytes.assign(reinterpret_cast<const char*>(unescaped), to_len);
            PQfreemem(unescaped);
        }
    }

    return AvatarModel{
        .user_id = user_id,
        .content_type = ct,
        .file_size = sz,
        .avatar_data = std::move(binary_bytes),
        .updated_at = updated_at
    };
}

Result<void> DbRepository::clear_user_avatars() {
    auto conn = DbPool::instance().acquire_writer();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    PgResultGuard res(conn->exec("TRUNCATE TABLE user_avatars;"));
    if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);
    return {};
}

Result<void> DbRepository::update_user_status(int64_t user_id, int status) {
    auto conn = DbPool::instance().acquire_writer();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    int64_t now = current_time_ms();
    conn->prepare("stmt_update_user_status", "UPDATE users SET status = $1, updated_at = $2 WHERE user_id = $3;", 3);
    std::string st_str = std::to_string(status);
    std::string now_str = std::to_string(now);
    std::string uid_str = std::to_string(user_id);
    const char* params[3] = { st_str.c_str(), now_str.c_str(), uid_str.c_str() };
    PgResultGuard res(conn->exec_prepared("stmt_update_user_status", 3, params));
    if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);

    // 同步更新 user_wallets 钱包账户状态
    conn->prepare("stmt_update_user_wallet_status", "UPDATE user_wallets SET status = $1, updated_at = $2 WHERE user_id = $3;", 3);
    conn->exec_prepared("stmt_update_user_wallet_status", 3, params);

    RedisCache::instance().del(std::format("cache:user:model:{}", user_id));
    RedisCache::instance().del(std::format("cache:user:wallet:{}", user_id));
    return {};
}

Result<std::vector<std::pair<int64_t, int64_t>>> DbRepository::get_frozen_users_info() {
    auto conn = DbPool::instance().acquire_reader();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    PgResultGuard res(conn->exec("SELECT user_id, updated_at FROM users WHERE status = 2;"));
    if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);

    std::vector<std::pair<int64_t, int64_t>> result;
    result.reserve(res.rows());
    for (int i = 0; i < res.rows(); ++i) {
        int64_t uid = std::stoll(res.value(i, 0));
        int64_t upd = 0;
        try {
            upd = std::stoll(res.value(i, 1));
        } catch (...) {
            upd = current_time_ms();
        }
        result.emplace_back(uid, upd);
    }
    return result;
}

Result<UserAdminListResponseData> DbRepository::get_users_admin_paged(
    int page,
    int page_size,
    std::string_view phone_filter,
    int status_filter
) {
    auto conn = DbPool::instance().acquire_reader();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    int offset = (page - 1) * page_size;
    std::string where = "WHERE 1=1";
    if (!phone_filter.empty()) {
        where += std::format(" AND u.phone LIKE '%{}%'", phone_filter);
    }
    if (status_filter > 0) {
        where += std::format(" AND u.status = {}", status_filter);
    }

    std::string count_sql = std::format("SELECT COUNT(*) FROM users u {};", where);
    PgResultGuard count_res(conn->exec(count_sql.c_str()));
    int64_t total = count_res.is_ok() && count_res.rows() > 0 ? std::stoll(count_res.value(0, 0)) : 0;

    std::string sql = std::format(
        "SELECT u.user_id, u.phone, u.nickname, COALESCE(w.balance_cents, 0), u.status, u.created_at "
        "FROM users u LEFT JOIN user_wallets w ON u.user_id = w.user_id "
        "{} ORDER BY u.created_at DESC LIMIT {} OFFSET {};",
        where, page_size, offset
    );

    PgResultGuard res(conn->exec(sql.c_str()));
    if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);

    UserAdminListResponseData data;
    data.total = total;
    data.page = page;
    data.page_size = page_size;

    for (int i = 0; i < res.rows(); ++i) {
        int64_t b_cents = std::stoll(res.value(i, 3));
        int st = std::stoi(res.value(i, 4));

        data.users.push_back(UserAdminItemDTO{
            .user_id = std::stoll(res.value(i, 0)),
            .phone = res.value(i, 1),
            .nickname = res.value(i, 2),
            .balance = cents_to_yuan(b_cents),
            .balance_cents = b_cents,
            .status = st,
            .status_desc = (st == 1 ? "NORMAL" : "FROZEN"),
            .created_at = std::stoll(res.value(i, 5))
        });
    }

    return data;
}

// ==========================================
// 2. 钱包与资金交易
// ==========================================

Result<UserWalletModel> DbRepository::get_wallet(int64_t user_id) {
    std::string cache_key = std::format("cache:wallet:{}", user_id);
    auto cached = RedisCache::instance().get_json<UserWalletModel>(cache_key);
    if (cached) {
        return *cached;
    }

    auto conn = DbPool::instance().acquire_reader();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    conn->prepare("stmt_get_wallet", "SELECT user_id, balance_cents, frozen_cents, status, updated_at FROM user_wallets WHERE user_id = $1;", 1);
    std::string uid_str = std::to_string(user_id);
    const char* params[1] = { uid_str.c_str() };
    PgResultGuard res(conn->exec_prepared("stmt_get_wallet", 1, params));

    if (!res.is_ok() || res.rows() == 0) {
        return std::unexpected(AppError::UserNotFound);
    }

    UserWalletModel w{
        .user_id = std::stoll(res.value(0, 0)),
        .balance_cents = std::stoll(res.value(0, 1)),
        .frozen_cents = std::stoll(res.value(0, 2)),
        .status = std::stoi(res.value(0, 3)),
        .updated_at = std::stoll(res.value(0, 4))
    };
    RedisCache::instance().set_json(cache_key, w, 60);
    return w;
}

Result<TransactionItemDTO> DbRepository::recharge_wallet(
    int64_t user_id,
    int64_t amount_cents,
    std::string_view idempotent_key,
    std::string_view remark
) {
    if (amount_cents <= 0) {
        return std::unexpected(AppError::InvalidAmount);
    }

    // 1. 内存幂等高速路径 (O(1) 快速查验，避免频繁穿透数据库)
    if (!idempotent_key.empty()) {
        auto cached = AsyncFlowPersister::instance().get_cached_idempotent(std::string(idempotent_key));
        if (cached) {
            return *cached;
        }
    }

    auto res = DbPool::instance().with_transaction([&](DbConnection& conn) -> Result<TransactionItemDTO> {
        // 2. 数据库持久化幂等校验 (兜底防重)
        if (!idempotent_key.empty()) {
            std::string chk_sql = std::format("SELECT id, flow_type, amount_cents, balance_before_cents, balance_after_cents, remark, created_at FROM wallet_transaction_flows WHERE idempotent_key = '{}';", idempotent_key);
            PgResultGuard chk_res(conn.exec(chk_sql.c_str()));
            if (chk_res.is_ok() && chk_res.rows() > 0) {
                int64_t amt = std::stoll(chk_res.value(0, 2));
                return TransactionItemDTO{
                    .transaction_id = chk_res.value(0, 0),
                    .flow_type = std::stoi(chk_res.value(0, 1)),
                    .flow_type_desc = "RECHARGE",
                    .amount = cents_to_yuan(amt),
                    .amount_cents = amt,
                    .balance_before = cents_to_yuan(std::stoll(chk_res.value(0, 3))),
                    .balance_after = cents_to_yuan(std::stoll(chk_res.value(0, 4))),
                    .related_order_id = "",
                    .remark = chk_res.value(0, 5),
                    .created_at = std::stoll(chk_res.value(0, 6))
                };
            }
        }

        // 行级锁锁定钱包行
        std::string lock_sql = std::format("SELECT balance_cents, status FROM user_wallets WHERE user_id = {} FOR UPDATE;", user_id);
        PgResultGuard lock_res(conn.exec(lock_sql.c_str()));
        if (!lock_res.is_ok() || lock_res.rows() == 0) {
            return std::unexpected(AppError::UserNotFound);
        }

        if (std::stoi(lock_res.value(0, 1)) == 2) {
            return std::unexpected(AppError::UserAccountFrozen);
        }

        int64_t before_cents = std::stoll(lock_res.value(0, 0));
        int64_t after_cents = before_cents + amount_cents;
        int64_t now = current_time_ms();
        std::string tx_id = std::format("TX_REC_{}_{}", now, user_id);

        // 更新余额 (核心事务仅包含钱包余额更新，耗时极短)
        std::string update_sql = std::format("UPDATE user_wallets SET balance_cents = {}, updated_at = {} WHERE user_id = {};", after_cents, now, user_id);
        PgResultGuard u_res(conn.exec(update_sql.c_str()));
        if (!u_res.is_ok()) return std::unexpected(AppError::DatabaseError);

        return TransactionItemDTO{
            .transaction_id = tx_id,
            .flow_type = 1,
            .flow_type_desc = "RECHARGE",
            .amount = cents_to_yuan(amount_cents),
            .amount_cents = amount_cents,
            .balance_before = cents_to_yuan(before_cents),
            .balance_after = cents_to_yuan(after_cents),
            .related_order_id = "",
            .remark = std::string(remark),
            .created_at = now
        };
    });

    // 3. 事务成功后异步投递流水落盘引擎并清除钱包缓存
    if (res) {
        RedisCache::instance().del(std::format("cache:wallet:{}", user_id));
        AsyncFlowPersister::instance().enqueue(WalletFlowItem{
            .id = res->transaction_id,
            .user_id = user_id,
            .flow_type = 1,
            .amount_cents = amount_cents,
            .balance_before_cents = yuan_to_cents(res->balance_before),
            .balance_after_cents = yuan_to_cents(res->balance_after),
            .related_order_id = "",
            .operator_id = 0,
            .remark = std::string(remark),
            .idempotent_key = std::string(idempotent_key),
            .created_at = res->created_at
        });
    }

    return res;
}

Result<TransactionListResponseData> DbRepository::get_transactions_paged(
    int64_t user_id,
    int page,
    int page_size,
    int flow_type
) {
    auto conn = DbPool::instance().acquire_reader();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    int offset = (page - 1) * page_size;
    std::string where = std::format("WHERE user_id = {}", user_id);
    if (flow_type > 0) {
        where += std::format(" AND flow_type = {}", flow_type);
    }

    std::string count_sql = std::format("SELECT COUNT(*) FROM wallet_transaction_flows {};", where);
    PgResultGuard count_res(conn->exec(count_sql.c_str()));
    int64_t total = count_res.is_ok() && count_res.rows() > 0 ? std::stoll(count_res.value(0, 0)) : 0;

    std::string sql = std::format(
        "SELECT id, flow_type, amount_cents, balance_before_cents, balance_after_cents, related_order_id, remark, created_at "
        "FROM wallet_transaction_flows {} ORDER BY created_at DESC LIMIT {} OFFSET {};",
        where, page_size, offset
    );

    PgResultGuard res(conn->exec(sql.c_str()));
    if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);

    TransactionListResponseData data;
    data.total = total;
    data.page = page;
    data.page_size = page_size;

    for (int i = 0; i < res.rows(); ++i) {
        int ft = std::stoi(res.value(i, 1));
        int64_t amt = std::stoll(res.value(i, 2));
        data.records.push_back(TransactionItemDTO{
            .transaction_id = res.value(i, 0),
            .flow_type = ft,
            .flow_type_desc = std::string(to_string(static_cast<FlowType>(ft))),
            .amount = cents_to_yuan(amt),
            .amount_cents = amt,
            .balance_before = cents_to_yuan(std::stoll(res.value(i, 3))),
            .balance_after = cents_to_yuan(std::stoll(res.value(i, 4))),
            .related_order_id = res.value(i, 5),
            .remark = res.value(i, 6),
            .created_at = std::stoll(res.value(i, 7))
        });
    }

    // 第一页查询时，融合内存中尚未落盘的最新流水，确保“写后即读”一致性
    if (page == 1) {
        auto pending = AsyncFlowPersister::instance().get_pending_for_user(user_id, flow_type);
        if (!pending.empty()) {
            std::unordered_set<std::string> seen_ids;
            for (const auto& r : data.records) {
                seen_ids.insert(r.transaction_id);
            }
            std::vector<TransactionItemDTO> unpersisted;
            for (auto& p : pending) {
                if (!seen_ids.contains(p.transaction_id)) {
                    unpersisted.push_back(std::move(p));
                }
            }
            if (!unpersisted.empty()) {
                data.total += unpersisted.size();
                unpersisted.insert(unpersisted.end(), data.records.begin(), data.records.end());
                if (unpersisted.size() > static_cast<size_t>(page_size)) {
                    unpersisted.resize(page_size);
                }
                data.records = std::move(unpersisted);
            }
        }
    }

    return data;
}

Result<UserWalletAdjustResponseData> DbRepository::adjust_user_wallet(
    int64_t user_id,
    int64_t amount_cents,
    int64_t operator_id,
    std::string_view idempotent_key,
    std::string_view remark
) {
    auto res = DbPool::instance().with_transaction([&](DbConnection& conn) -> Result<UserWalletAdjustResponseData> {
        // 行级锁
        std::string lock_sql = std::format("SELECT balance_cents FROM user_wallets WHERE user_id = {} FOR UPDATE;", user_id);
        PgResultGuard lock_res(conn.exec(lock_sql.c_str()));
        if (!lock_res.is_ok() || lock_res.rows() == 0) {
            return std::unexpected(AppError::UserNotFound);
        }

        int64_t before_cents = std::stoll(lock_res.value(0, 0));
        int64_t after_cents = before_cents + amount_cents;
        if (after_cents < 0) {
            return std::unexpected(AppError::InsufficientBalance);
        }

        int64_t now = current_time_ms();
        std::string tx_id = std::format("TX_ADJ_{}_{}", now, user_id);

        std::string u_sql = std::format("UPDATE user_wallets SET balance_cents = {}, updated_at = {} WHERE user_id = {};", after_cents, now, user_id);
        conn.exec(u_sql.c_str());

        return UserWalletAdjustResponseData{
            .transaction_id = tx_id,
            .user_id = user_id,
            .adjust_amount = cents_to_yuan(amount_cents),
            .balance_before = cents_to_yuan(before_cents),
            .balance_after = cents_to_yuan(after_cents),
            .operator_id = operator_id,
            .created_at = now
        };
    });

    if (res) {
        RedisCache::instance().del(std::format("cache:wallet:{}", user_id));
        AsyncFlowPersister::instance().enqueue(WalletFlowItem{
            .id = res->transaction_id,
            .user_id = user_id,
            .flow_type = 4,
            .amount_cents = amount_cents,
            .balance_before_cents = yuan_to_cents(res->balance_before),
            .balance_after_cents = yuan_to_cents(res->balance_after),
            .related_order_id = "",
            .operator_id = operator_id,
            .remark = std::string(remark),
            .idempotent_key = std::string(idempotent_key),
            .created_at = res->created_at
        });
    }

    return res;
}

// ==========================================
// 3. 充电站管理与销售分析
// ==========================================

Result<std::vector<StationModel>> DbRepository::get_all_stations() {
    auto conn = DbPool::instance().acquire_reader();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    std::string sql = "SELECT station_id, station_name, address, latitude, longitude, contact_phone, operating_hours, price_per_kwh, service_fee_per_kwh, overtime_fee_per_15min, overtime_grace_minutes, status, created_at, updated_at FROM stations ORDER BY station_id ASC;";
    PgResultGuard res(conn->exec(sql.c_str()));
    if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);

    std::vector<StationModel> stations;
    for (int i = 0; i < res.rows(); ++i) {
        stations.push_back(StationModel{
            .station_id = std::stoll(res.value(i, 0)),
            .station_name = res.value(i, 1),
            .address = res.value(i, 2),
            .latitude = std::stod(res.value(i, 3)),
            .longitude = std::stod(res.value(i, 4)),
            .contact_phone = res.value(i, 5),
            .operating_hours = res.value(i, 6),
            .price_per_kwh = std::stod(res.value(i, 7)),
            .service_fee_per_kwh = std::stod(res.value(i, 8)),
            .overtime_fee_per_15min = std::stod(res.value(i, 9)),
            .overtime_grace_minutes = std::stoi(res.value(i, 10)),
            .status = std::stoi(res.value(i, 11)),
            .created_at = std::stoll(res.value(i, 12)),
            .updated_at = std::stoll(res.value(i, 13))
        });
    }
    return stations;
}

Result<StationModel> DbRepository::get_station_by_id(int64_t station_id) {
    auto conn = DbPool::instance().acquire_reader();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    std::string sql = std::format("SELECT station_id, station_name, address, latitude, longitude, contact_phone, operating_hours, price_per_kwh, service_fee_per_kwh, overtime_fee_per_15min, overtime_grace_minutes, status, created_at, updated_at FROM stations WHERE station_id = {};", station_id);
    PgResultGuard res(conn->exec(sql.c_str()));
    if (!res.is_ok() || res.rows() == 0) return std::unexpected(AppError::StationNotFound);

    return StationModel{
        .station_id = std::stoll(res.value(0, 0)),
        .station_name = res.value(0, 1),
        .address = res.value(0, 2),
        .latitude = std::stod(res.value(0, 3)),
        .longitude = std::stod(res.value(0, 4)),
        .contact_phone = res.value(0, 5),
        .operating_hours = res.value(0, 6),
        .price_per_kwh = std::stod(res.value(0, 7)),
        .service_fee_per_kwh = std::stod(res.value(0, 8)),
        .overtime_fee_per_15min = std::stod(res.value(0, 9)),
        .overtime_grace_minutes = std::stoi(res.value(0, 10)),
        .status = std::stoi(res.value(0, 11)),
        .created_at = std::stoll(res.value(0, 12)),
        .updated_at = std::stoll(res.value(0, 13))
    };
}

Result<StationAdminListResponseData> DbRepository::get_stations_admin_paged(
    int page,
    int page_size,
    std::string_view name_filter,
    int status_filter
) {
    auto conn = DbPool::instance().acquire_reader();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    int offset = (page - 1) * page_size;
    std::string where = "WHERE 1=1";
    if (!name_filter.empty()) {
        where += std::format(" AND station_name LIKE '%{}%'", name_filter);
    }
    if (status_filter > 0) {
        where += std::format(" AND status = {}", status_filter);
    }

    std::string count_sql = std::format("SELECT COUNT(*) FROM stations {};", where);
    PgResultGuard count_res(conn->exec(count_sql.c_str()));
    int64_t total = count_res.is_ok() && count_res.rows() > 0 ? std::stoll(count_res.value(0, 0)) : 0;

    std::string sql = std::format(
        "SELECT s.station_id, s.station_name, s.address, s.latitude, s.longitude, "
        "s.price_per_kwh, s.service_fee_per_kwh, s.overtime_fee_per_15min, s.status, s.created_at "
        "FROM stations s "
        "{} ORDER BY s.station_id ASC LIMIT {} OFFSET {};",
        where, page_size, offset
    );

    PgResultGuard res(conn->exec(sql.c_str()));
    if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);

    StationAdminListResponseData data;
    data.total = total;
    data.page = page;
    data.page_size = page_size;

    for (int i = 0; i < res.rows(); ++i) {
        int64_t sid = std::stoll(res.value(i, 0));
        auto piles = ChargingStatePool::instance().get_piles_by_station(sid);
        int tot = static_cast<int>(piles.size());
        int idle = 0;
        int online = 0;
        for (const auto& p : piles) {
            if (p.status == "IDLE") idle++;
            if (p.status != "FAULT" && p.status != "OFFLINE") online++;
        }
        double rate = tot > 0 ? (static_cast<double>(online) / tot * 100.0) : 100.0;

        data.stations.push_back(StationAdminItemDTO{
            .station_id = sid,
            .station_name = res.value(i, 1),
            .address = res.value(i, 2),
            .latitude = std::stod(res.value(i, 3)),
            .longitude = std::stod(res.value(i, 4)),
            .total_piles = tot,
            .online_piles = online,
            .idle_piles = idle,
            .online_rate = rate,
            .price_per_kwh = std::stod(res.value(i, 5)),
            .service_fee_per_kwh = std::stod(res.value(i, 6)),
            .overtime_fee_per_15min = std::stod(res.value(i, 7)),
            .status = std::stoi(res.value(i, 8)),
            .created_at = std::stoll(res.value(i, 9))
        });
    }

    return data;
}

Result<int64_t> DbRepository::create_station(const CreateStationRequest& req) {
    auto conn = DbPool::instance().acquire_writer();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    int64_t now = current_time_ms();
    std::string sql = std::format(
        "INSERT INTO stations (station_name, address, latitude, longitude, contact_phone, operating_hours, price_per_kwh, service_fee_per_kwh, overtime_fee_per_15min, overtime_grace_minutes, status, created_at, updated_at) "
        "VALUES ('{}', '{}', {}, {}, '{}', '00:00 - 24:00', {}, {}, {}, 15, 1, {}, {}) RETURNING station_id;",
        req.station_name, req.address, req.latitude, req.longitude, req.contact_phone, req.price_per_kwh, req.service_fee_per_kwh, req.overtime_fee_per_15min, now, now
    );

    PgResultGuard res(conn->exec(sql.c_str()));
    if (!res.is_ok() || res.rows() == 0) return std::unexpected(AppError::DatabaseError);
    return std::stoll(res.value(0, 0));
}

Result<void> DbRepository::update_station(int64_t station_id, const UpdateStationRequest& req) {
    auto conn = DbPool::instance().acquire_writer();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    int64_t now = current_time_ms();
    std::string sql = std::format(
        "UPDATE stations SET station_name = '{}', price_per_kwh = {}, service_fee_per_kwh = {}, overtime_fee_per_15min = {}, status = {}, updated_at = {} WHERE station_id = {};",
        req.station_name, req.price_per_kwh, req.service_fee_per_kwh, req.overtime_fee_per_15min, req.status, now, station_id
    );

    PgResultGuard res(conn->exec(sql.c_str()));
    if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);

    StationPriceManager::instance().set_price(station_id, req.price_per_kwh);
    return {};
}

Result<void> DbRepository::delete_station(int64_t station_id) {
    auto conn = DbPool::instance().acquire_writer();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    std::string sql = std::format("DELETE FROM stations WHERE station_id = {};", station_id);
    PgResultGuard res(conn->exec(sql.c_str()));
    if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);
    return {};
}

Result<StationSalesStatsResponseData> DbRepository::get_station_sales_stats(int64_t station_id, std::string_view time_range) {
    auto st_res = get_station_by_id(station_id);
    if (!st_res) return std::unexpected(AppError::StationNotFound);

    auto conn = DbPool::instance().acquire_reader();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    int64_t now = current_time_ms();
    int64_t cst_offset = 8 * 3600 * 1000LL;
    int64_t today_start = ((now + cst_offset) / 86400000LL) * 86400000LL - cst_offset;
    int64_t start_threshold = 0;

    if (time_range == "today") {
        start_threshold = today_start;
    } else if (time_range == "7d") {
        start_threshold = today_start - 6LL * 86400000LL;
    } else { // 30d
        start_threshold = today_start - 29LL * 86400000LL;
    }

    std::string sum_sql = std::format(
        "SELECT COALESCE(SUM(total_fee_cents), 0), COALESCE(SUM(electricity_fee_cents), 0), "
        "COALESCE(SUM(service_fee_cents), 0), COALESCE(SUM(overtime_fee_cents), 0), "
        "COALESCE(SUM(charged_energy_kwh), 0.0), COUNT(*) "
        "FROM charging_orders WHERE station_id = {} AND order_status IN ('COMPLETED', 'UNSETTLED') AND created_at >= {};",
        station_id, start_threshold
    );

    PgResultGuard sum_res(conn->exec(sum_sql.c_str()));
    if (!sum_res.is_ok()) return std::unexpected(AppError::DatabaseError);

    int64_t tot_cents = std::stoll(sum_res.value(0, 0));
    int64_t elec_cents = std::stoll(sum_res.value(0, 1));
    int64_t serv_cents = std::stoll(sum_res.value(0, 2));
    int64_t over_cents = std::stoll(sum_res.value(0, 3));
    double tot_energy = std::stod(sum_res.value(0, 4));
    int64_t tot_orders = std::stoll(sum_res.value(0, 5));
    double avg_amount = tot_orders > 0 ? (cents_to_yuan(tot_cents) / tot_orders) : 0.0;

    StationSalesStatsResponseData data;
    data.station_id = station_id;
    data.station_name = st_res->station_name;
    data.time_range = std::string(time_range);
    data.summary = StationSalesSummaryDTO{
        .total_revenue = cents_to_yuan(tot_cents),
        .total_revenue_cents = tot_cents,
        .electricity_fee_total = cents_to_yuan(elec_cents),
        .service_fee_total = cents_to_yuan(serv_cents),
        .overtime_fee_total = cents_to_yuan(over_cents),
        .total_energy_kwh = tot_energy,
        .total_order_count = tot_orders,
        .average_order_amount = avg_amount
    };

    if (time_range == "today") {
        // 当日按 4 小时间隔切片: 00:00, 04:00, 08:00, 12:00, 16:00, 20:00
        data.timeline.time_slots = {"00:00", "04:00", "08:00", "12:00", "16:00", "20:00"};
        data.timeline.revenue_series.assign(6, 0.0);
        data.timeline.energy_series.assign(6, 0.0);
        data.timeline.order_series.assign(6, 0);

        std::string slot_sql = std::format(
            "SELECT ((created_at - {}) / 14400000) as slot_idx, "
            "COALESCE(SUM(total_fee_cents), 0), COALESCE(SUM(charged_energy_kwh), 0.0), COUNT(*) "
            "FROM charging_orders "
            "WHERE station_id = {} AND order_status IN ('COMPLETED', 'UNSETTLED') AND created_at >= {} "
            "GROUP BY slot_idx;",
            start_threshold, station_id, start_threshold
        );
        PgResultGuard slot_res(conn->exec(slot_sql.c_str()));
        if (slot_res.is_ok()) {
            for (int r = 0; r < slot_res.rows(); ++r) {
                int64_t s_idx = std::stoll(slot_res.value(r, 0));
                if (s_idx >= 0 && s_idx < 6) {
                    int64_t rev_c = std::stoll(slot_res.value(r, 1));
                    double energy = std::stod(slot_res.value(r, 2));
                    int64_t ord_c = std::stoll(slot_res.value(r, 3));
                    data.timeline.revenue_series[s_idx] = cents_to_yuan(rev_c);
                    data.timeline.energy_series[s_idx] = energy;
                    data.timeline.order_series[s_idx] = ord_c;
                }
            }
        }
    } else {
        int days = (time_range == "7d") ? 7 : 30;
        data.timeline.revenue_series.assign(days, 0.0);
        data.timeline.energy_series.assign(days, 0.0);
        data.timeline.order_series.assign(days, 0);

        for (int d = 0; d < days; ++d) {
            int64_t day_ms = start_threshold + static_cast<int64_t>(d) * 86400000LL;
            time_t t = day_ms / 1000;
            struct tm* tm_info = localtime(&t);
            char buf[32];
            strftime(buf, sizeof(buf), "%Y-%m-%d", tm_info);
            data.timeline.time_slots.push_back(buf);
        }

        std::string day_sql = std::format(
            "SELECT ((created_at - {}) / 86400000) as day_idx, "
            "COALESCE(SUM(total_fee_cents), 0), COALESCE(SUM(charged_energy_kwh), 0.0), COUNT(*) "
            "FROM charging_orders "
            "WHERE station_id = {} AND order_status IN ('COMPLETED', 'UNSETTLED') AND created_at >= {} "
            "GROUP BY day_idx;",
            start_threshold, station_id, start_threshold
        );
        PgResultGuard day_res(conn->exec(day_sql.c_str()));
        if (day_res.is_ok()) {
            for (int r = 0; r < day_res.rows(); ++r) {
                int64_t d_idx = std::stoll(day_res.value(r, 0));
                if (d_idx >= 0 && d_idx < days) {
                    int64_t rev_c = std::stoll(day_res.value(r, 1));
                    double energy = std::stod(day_res.value(r, 2));
                    int64_t ord_c = std::stoll(day_res.value(r, 3));
                    data.timeline.revenue_series[d_idx] = cents_to_yuan(rev_c);
                    data.timeline.energy_series[d_idx] = energy;
                    data.timeline.order_series[d_idx] = ord_c;
                }
            }
        }
    }

    return data;
}

// ==========================================
// 4. 充电桩管理
// ==========================================

Result<std::vector<PileModel>> DbRepository::get_all_piles() {
    auto conn = DbPool::instance().acquire_reader();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    std::string sql = "SELECT pile_id, station_id, pile_name, type, gun_type, max_power_kw, voltage_range, total_charge_count, total_charge_hours, last_heartbeat_at, created_at, updated_at FROM piles;";
    PgResultGuard res(conn->exec(sql.c_str()));
    if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);

    std::vector<PileModel> piles;
    piles.reserve(res.rows());
    for (int i = 0; i < res.rows(); ++i) {
        std::string pid = res.value(i, 0);
        piles.push_back(PileModel{
            .pile_id = pid,
            .station_id = std::stoll(res.value(i, 1)),
            .pile_name = res.value(i, 2),
            .type = res.value(i, 3),
            .gun_type = res.value(i, 4),
            .max_power_kw = std::stod(res.value(i, 5)),
            .voltage_range = res.value(i, 6),
            .status = std::string(ChargingStatePool::instance().get_pile_status(pid)),
            .total_charge_count = std::stoll(res.value(i, 7)),
            .total_charge_hours = std::stod(res.value(i, 8)),
            .last_heartbeat_at = std::stoll(res.value(i, 9)),
            .created_at = std::stoll(res.value(i, 10)),
            .updated_at = std::stoll(res.value(i, 11))
        });
    }
    return piles;
}

Result<std::vector<PileModel>> DbRepository::get_piles_by_station(int64_t station_id) {
    auto conn = DbPool::instance().acquire_reader();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    std::string sql = std::format(
        "SELECT pile_id, station_id, pile_name, type, gun_type, max_power_kw, voltage_range, total_charge_count, total_charge_hours, last_heartbeat_at, created_at, updated_at "
        "FROM piles WHERE station_id = {} ORDER BY pile_id ASC;",
        station_id
    );

    PgResultGuard res(conn->exec(sql.c_str()));
    if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);

    std::vector<PileModel> piles;
    for (int i = 0; i < res.rows(); ++i) {
        std::string pid = res.value(i, 0);
        piles.push_back(PileModel{
            .pile_id = pid,
            .station_id = std::stoll(res.value(i, 1)),
            .pile_name = res.value(i, 2),
            .type = res.value(i, 3),
            .gun_type = res.value(i, 4),
            .max_power_kw = std::stod(res.value(i, 5)),
            .voltage_range = res.value(i, 6),
            .status = std::string(ChargingStatePool::instance().get_pile_status(pid)),
            .total_charge_count = std::stoll(res.value(i, 7)),
            .total_charge_hours = std::stod(res.value(i, 8)),
            .last_heartbeat_at = std::stoll(res.value(i, 9)),
            .created_at = std::stoll(res.value(i, 10)),
            .updated_at = std::stoll(res.value(i, 11))
        });
    }

    return piles;
}

Result<PileModel> DbRepository::get_pile_by_id(std::string_view pile_id) {
    auto conn = DbPool::instance().acquire_reader();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    std::string sql = std::format(
        "SELECT pile_id, station_id, pile_name, type, gun_type, max_power_kw, voltage_range, total_charge_count, total_charge_hours, last_heartbeat_at, created_at, updated_at "
        "FROM piles WHERE pile_id = '{}' OR LOWER(pile_id) = LOWER('{}') LIMIT 1;",
        pile_id, pile_id
    );

    PgResultGuard res(conn->exec(sql.c_str()));
    if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);
    if (res.rows() == 0) return std::unexpected(AppError::ChargingPileNotFound);

    std::string pid = res.value(0, 0);
    return PileModel{
        .pile_id = pid,
        .station_id = std::stoll(res.value(0, 1)),
        .pile_name = res.value(0, 2),
        .type = res.value(0, 3),
        .gun_type = res.value(0, 4),
        .max_power_kw = std::stod(res.value(0, 5)),
        .voltage_range = res.value(0, 6),
        .status = std::string(ChargingStatePool::instance().get_pile_status(pid)),
        .total_charge_count = std::stoll(res.value(0, 7)),
        .total_charge_hours = std::stod(res.value(0, 8)),
        .last_heartbeat_at = std::stoll(res.value(0, 9)),
        .created_at = std::stoll(res.value(0, 10)),
        .updated_at = std::stoll(res.value(0, 11))
    };
}

Result<PileListResponseData> DbRepository::get_piles_paged(
    int page,
    int page_size,
    int64_t station_id_filter,
    std::string_view status_filter,
    std::string_view type_filter
) {
    return ChargingStatePool::instance().get_piles_paged(
        page, page_size, station_id_filter, status_filter, type_filter
    );
}

Result<void> DbRepository::create_pile(const CreatePileRequest& req) {
    auto conn = DbPool::instance().acquire_writer();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    int64_t now = current_time_ms();
    double pwr = (req.max_power_kw > 0.0) ? req.max_power_kw : ((req.power_kw > 0.0) ? req.power_kw : 120.0);
    std::string v_range = req.voltage_range.empty() ? "200V-750V" : req.voltage_range;

    std::string sql = std::format(
        "INSERT INTO piles (pile_id, station_id, pile_name, type, gun_type, max_power_kw, voltage_range, total_charge_count, total_charge_hours, last_heartbeat_at, created_at, updated_at) "
        "VALUES ('{}', {}, '{}', '{}', '{}', {}, '{}', 0, 0.0, {}, {}, {});",
        req.pile_id, req.station_id, req.pile_name, req.type, req.gun_type, pwr, v_range, now, now, now
    );

    PgResultGuard res(conn->exec(sql.c_str()));
    if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);

    // 内存池注册新桩
    ChargingStatePool::instance().register_pile(req.pile_id, req.station_id, req.pile_name, req.type, pwr);

    return {};
}

Result<void> DbRepository::update_pile_status(std::string_view pile_id, std::string_view status) {
    ChargingStatePool::instance().set_pile_status(pile_id, status);

    auto conn = DbPool::instance().acquire_writer();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    int64_t now = current_time_ms();
    std::string sql = std::format("UPDATE piles SET last_heartbeat_at = {}, updated_at = {} WHERE pile_id = '{}';", now, now, pile_id);
    PgResultGuard res(conn->exec(sql.c_str()));
    if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);
    return {};
}

Result<void> DbRepository::update_pile_metrics(std::string_view pile_id, int64_t add_count, double add_hours) {
    ChargingStatePool::instance().increment_charge_stats(pile_id, add_hours);

    auto conn = DbPool::instance().acquire_writer();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    int64_t now = current_time_ms();
    std::string sql = std::format("UPDATE piles SET total_charge_count = total_charge_count + {}, total_charge_hours = total_charge_hours + {}, updated_at = {} WHERE pile_id = '{}';", add_count, add_hours, now, pile_id);
    PgResultGuard res(conn->exec(sql.c_str()));
    if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);
    return {};
}

// ==========================================
// 5. 充电业务流程与订单
// ==========================================

Result<std::optional<OrderModel>> DbRepository::get_active_order_by_user(int64_t user_id) {
    std::string cache_key = std::format("cache:order:active:{}", user_id);
    auto cached = RedisCache::instance().get_json<std::optional<OrderModel>>(cache_key);
    if (cached) {
        return *cached;
    }

    auto conn = DbPool::instance().acquire_reader();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    conn->prepare("stmt_get_active_order",
        "SELECT order_id, user_id, station_id, pile_id, strategy_type, strategy_value, order_status, "
        "start_time, end_time, start_soc, end_soc, charged_energy_kwh, electricity_price, electricity_fee_cents, "
        "service_price, service_fee_cents, overtime_grace_minutes, overtime_duration_minutes, overtime_rate_per_15min, "
        "overtime_fee_cents, total_fee_cents, stop_reason, settled_at, refund_transaction_id, operator_id, "
        "refund_reason, refunded_at, created_at, updated_at "
        "FROM charging_orders WHERE user_id = $1 AND order_status IN ('CHARGING', 'UNSETTLED') LIMIT 1;", 1);

    std::string uid_str = std::to_string(user_id);
    const char* params[1] = { uid_str.c_str() };
    PgResultGuard res(conn->exec_prepared("stmt_get_active_order", 1, params));

    if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);
    if (res.rows() == 0) {
        std::optional<OrderModel> empty_opt = std::nullopt;
        RedisCache::instance().set_json(cache_key, empty_opt, 5);
        return empty_opt;
    }

    OrderModel order;
    order.order_id = res.value(0, 0);
    order.user_id = std::stoll(res.value(0, 1));
    order.station_id = std::stoll(res.value(0, 2));
    order.pile_id = res.value(0, 3);
    order.strategy_type = res.value(0, 4);
    order.strategy_value = std::stod(res.value(0, 5));
    order.order_status = res.value(0, 6);
    order.start_time = std::stoll(res.value(0, 7));
    order.end_time = std::stoll(res.value(0, 8));
    order.start_soc = std::stoi(res.value(0, 9));
    order.end_soc = std::stoi(res.value(0, 10));
    order.charged_energy_kwh = std::stod(res.value(0, 11));
    order.electricity_price = std::stod(res.value(0, 12));
    order.electricity_fee_cents = std::stoll(res.value(0, 13));
    order.service_price = std::stod(res.value(0, 14));
    order.service_fee_cents = std::stoll(res.value(0, 15));
    order.overtime_grace_minutes = std::stoi(res.value(0, 16));
    order.overtime_duration_minutes = std::stoi(res.value(0, 17));
    order.overtime_rate_per_15min = std::stod(res.value(0, 18));
    order.overtime_fee_cents = std::stoll(res.value(0, 19));
    order.total_fee_cents = std::stoll(res.value(0, 20));
    order.stop_reason = res.value(0, 21);
    order.settled_at = std::stoll(res.value(0, 22));
    order.refund_transaction_id = res.value(0, 23);
    order.operator_id = std::stoll(res.value(0, 24));
    order.refund_reason = res.value(0, 25);
    order.refunded_at = std::stoll(res.value(0, 26));
    order.created_at = std::stoll(res.value(0, 27));
    order.updated_at = std::stoll(res.value(0, 28));

    std::optional<OrderModel> opt = order;
    RedisCache::instance().set_json(cache_key, opt, 5);
    return opt;
}

Result<OrderModel> DbRepository::get_order_by_id(std::string_view order_id) {
    auto conn = DbPool::instance().acquire_reader();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    std::string sql = std::format(
        "SELECT order_id, user_id, station_id, pile_id, strategy_type, strategy_value, order_status, "
        "start_time, end_time, start_soc, end_soc, charged_energy_kwh, electricity_price, electricity_fee_cents, "
        "service_price, service_fee_cents, overtime_grace_minutes, overtime_duration_minutes, overtime_rate_per_15min, "
        "overtime_fee_cents, total_fee_cents, stop_reason, settled_at, refund_transaction_id, operator_id, "
        "refund_reason, refunded_at, created_at, updated_at "
        "FROM charging_orders WHERE order_id = '{}';",
        order_id
    );

    PgResultGuard res(conn->exec(sql.c_str()));
    if (!res.is_ok() || res.rows() == 0) return std::unexpected(AppError::OrderNotFound);

    OrderModel order;
    order.order_id = res.value(0, 0);
    order.user_id = std::stoll(res.value(0, 1));
    order.station_id = std::stoll(res.value(0, 2));
    order.pile_id = res.value(0, 3);
    order.strategy_type = res.value(0, 4);
    order.strategy_value = std::stod(res.value(0, 5));
    order.order_status = res.value(0, 6);
    order.start_time = std::stoll(res.value(0, 7));
    order.end_time = std::stoll(res.value(0, 8));
    order.start_soc = std::stoi(res.value(0, 9));
    order.end_soc = std::stoi(res.value(0, 10));
    order.charged_energy_kwh = std::stod(res.value(0, 11));
    order.electricity_price = std::stod(res.value(0, 12));
    order.electricity_fee_cents = std::stoll(res.value(0, 13));
    order.service_price = std::stod(res.value(0, 14));
    order.service_fee_cents = std::stoll(res.value(0, 15));
    order.overtime_grace_minutes = std::stoi(res.value(0, 16));
    order.overtime_duration_minutes = std::stoi(res.value(0, 17));
    order.overtime_rate_per_15min = std::stod(res.value(0, 18));
    order.overtime_fee_cents = std::stoll(res.value(0, 19));
    order.total_fee_cents = std::stoll(res.value(0, 20));
    order.stop_reason = res.value(0, 21);
    order.settled_at = std::stoll(res.value(0, 22));
    order.refund_transaction_id = res.value(0, 23);
    order.operator_id = std::stoll(res.value(0, 24));
    order.refund_reason = res.value(0, 25);
    order.refunded_at = std::stoll(res.value(0, 26));
    order.created_at = std::stoll(res.value(0, 27));
    order.updated_at = std::stoll(res.value(0, 28));

    return order;
}

Result<void> DbRepository::create_order(const OrderModel& order) {
    auto res = DbPool::instance().with_transaction([&](DbConnection& conn) -> Result<void> {
        std::string sql = std::format(
            "INSERT INTO charging_orders (order_id, user_id, station_id, pile_id, strategy_type, strategy_value, order_status, start_time, end_time, start_soc, end_soc, charged_energy_kwh, electricity_price, electricity_fee_cents, service_price, service_fee_cents, overtime_grace_minutes, overtime_duration_minutes, overtime_rate_per_15min, overtime_fee_cents, total_fee_cents, stop_reason, settled_at, created_at, updated_at) "
            "VALUES ('{}', {}, {}, '{}', '{}', {}, '{}', {}, 0, {}, {}, 0.0, {}, 0, {}, 0, {}, 0, {}, 0, 0, '', 0, {}, {});",
            order.order_id, order.user_id, order.station_id, order.pile_id, order.strategy_type, order.strategy_value, order.order_status, order.start_time, order.start_soc, order.end_soc, order.electricity_price, order.service_price, order.overtime_grace_minutes, order.overtime_rate_per_15min, order.created_at, order.updated_at
        );

        PgResultGuard res(conn.exec(sql.c_str()));
        if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);

        return {};
    });

    if (res) {
        RedisCache::instance().del(std::format("cache:order:active:{}", order.user_id));
    }
    return res;
}

Result<StopChargingResponseData> DbRepository::stop_order(
    std::string_view order_id,
    int64_t end_time,
    int end_soc,
    double energy_kwh,
    int64_t elec_fee_cents,
    int64_t serv_fee_cents,
    int overtime_mins,
    int64_t overtime_fee_cents,
    int64_t total_fee_cents,
    std::string_view stop_reason
) {
    auto o_res = get_order_by_id(order_id);
    if (!o_res) return std::unexpected(AppError::OrderNotFound);
    if (o_res->order_status != "CHARGING") return std::unexpected(AppError::OrderCannotBeStopped);

    return DbPool::instance().with_transaction([&](DbConnection& conn) -> Result<StopChargingResponseData> {
        int64_t duration = (end_time - o_res->start_time) / 1000;
        if (duration < 0) duration = 0;

        std::string sql = std::format(
            "UPDATE charging_orders SET order_status = 'UNSETTLED', end_time = {}, end_soc = {}, charged_energy_kwh = {}, electricity_fee_cents = {}, service_fee_cents = {}, overtime_duration_minutes = {}, overtime_fee_cents = {}, total_fee_cents = {}, stop_reason = '{}', updated_at = {} "
            "WHERE order_id = '{}';",
            end_time, end_soc, energy_kwh, elec_fee_cents, serv_fee_cents, overtime_mins, overtime_fee_cents, total_fee_cents, stop_reason, end_time, order_id
        );

        PgResultGuard res(conn.exec(sql.c_str()));
        if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);

        // 桩位指标更新 (DB 与 内存池)
        double add_h = static_cast<double>(duration) / 3600.0;
        std::string p_sql = std::format("UPDATE piles SET total_charge_count = total_charge_count + 1, total_charge_hours = total_charge_hours + {}, updated_at = {} WHERE pile_id = '{}';", add_h, end_time, o_res->pile_id);
        conn.exec(p_sql.c_str());
        ChargingStatePool::instance().increment_charge_stats(o_res->pile_id, add_h);
        ChargingStatePool::instance().set_pile_status(o_res->pile_id, "IDLE");

        return StopChargingResponseData{
            .order_id = std::string(order_id),
            .order_status = "UNSETTLED",
            .end_time = end_time,
            .duration_seconds = duration,
            .charged_energy_kwh = energy_kwh,
            .final_soc = end_soc,
            .electricity_fee = cents_to_yuan(elec_fee_cents),
            .service_fee = cents_to_yuan(serv_fee_cents),
            .overtime_minutes = overtime_mins,
            .overtime_fee = cents_to_yuan(overtime_fee_cents),
            .total_amount = cents_to_yuan(total_fee_cents),
            .total_amount_cents = total_fee_cents,
            .need_settle = true
        };
    });
}

Result<SettleOrderResponseData> DbRepository::settle_order_with_wallet(
    std::string_view order_id,
    std::string_view idempotent_key
) {
    int64_t settled_user_id = 0;
    int64_t before_balance_cents = 0;
    bool is_newly_settled = false;

    auto res = DbPool::instance().with_transaction([&](DbConnection& conn) -> Result<SettleOrderResponseData> {
        // 锁定订单行
        std::string o_sql = std::format("SELECT user_id, order_status, total_fee_cents, electricity_fee_cents, service_fee_cents, overtime_fee_cents FROM charging_orders WHERE order_id = '{}' FOR UPDATE;", order_id);
        PgResultGuard o_res(conn.exec(o_sql.c_str()));
        if (!o_res.is_ok() || o_res.rows() == 0) return std::unexpected(AppError::OrderNotFound);

        std::string st = o_res.value(0, 1);
        if (st == "COMPLETED") {
            // 已结算过，直接查询钱包返回
            int64_t uid = std::stoll(o_res.value(0, 0));
            int64_t fee_cents = std::stoll(o_res.value(0, 2));
            auto w = get_wallet(uid);
            return SettleOrderResponseData{
                .order_id = std::string(order_id),
                .order_status = "COMPLETED",
                .electricity_fee = cents_to_yuan(std::stoll(o_res.value(0, 3))),
                .service_fee = cents_to_yuan(std::stoll(o_res.value(0, 4))),
                .overtime_fee = cents_to_yuan(std::stoll(o_res.value(0, 5))),
                .total_fee = cents_to_yuan(fee_cents),
                .total_fee_cents = fee_cents,
                .wallet_deducted = cents_to_yuan(fee_cents),
                .new_balance = w ? cents_to_yuan(w->balance_cents) : 0.0,
                .new_balance_cents = w ? w->balance_cents : 0,
                .settled_at = current_time_ms()
            };
        }

        if (st != "UNSETTLED") {
            return std::unexpected(AppError::OrderCannotBeSettled);
        }

        int64_t user_id = std::stoll(o_res.value(0, 0));
        int64_t total_fee_cents = std::stoll(o_res.value(0, 2));
        int64_t elec_cents = std::stoll(o_res.value(0, 3));
        int64_t serv_cents = std::stoll(o_res.value(0, 4));
        int64_t over_cents = std::stoll(o_res.value(0, 5));

        // 锁定钱包行
        std::string w_sql = std::format("SELECT balance_cents, status FROM user_wallets WHERE user_id = {} FOR UPDATE;", user_id);
        PgResultGuard w_res(conn.exec(w_sql.c_str()));
        if (!w_res.is_ok() || w_res.rows() == 0) return std::unexpected(AppError::UserNotFound);

        if (std::stoi(w_res.value(0, 1)) == 2) return std::unexpected(AppError::UserAccountFrozen);

        int64_t before_cents = std::stoll(w_res.value(0, 0));
        if (before_cents < total_fee_cents) {
            return std::unexpected(AppError::InsufficientBalance);
        }

        int64_t after_cents = before_cents - total_fee_cents;
        int64_t now = current_time_ms();
        std::string tx_id = std::format("TX_ORD_{}_{}", now, user_id);

        settled_user_id = user_id;
        before_balance_cents = before_cents;
        is_newly_settled = true;

        // 扣款更新
        std::string u_wallet = std::format("UPDATE user_wallets SET balance_cents = {}, updated_at = {} WHERE user_id = {};", after_cents, now, user_id);
        conn.exec(u_wallet.c_str());

        // 订单状态置为 COMPLETED
        std::string u_order = std::format("UPDATE charging_orders SET order_status = 'COMPLETED', settled_at = {}, updated_at = {} WHERE order_id = '{}';", now, now, order_id);
        conn.exec(u_order.c_str());

        return SettleOrderResponseData{
            .order_id = std::string(order_id),
            .order_status = "COMPLETED",
            .electricity_fee = cents_to_yuan(elec_cents),
            .service_fee = cents_to_yuan(serv_cents),
            .overtime_fee = cents_to_yuan(over_cents),
            .total_fee = cents_to_yuan(total_fee_cents),
            .total_fee_cents = total_fee_cents,
            .wallet_deducted = cents_to_yuan(total_fee_cents),
            .new_balance = cents_to_yuan(after_cents),
            .new_balance_cents = after_cents,
            .settled_at = now
        };
    });

    if (res && is_newly_settled) {
        AsyncFlowPersister::instance().enqueue(WalletFlowItem{
            .id = std::format("TX_ORD_{}_{}", res->settled_at, settled_user_id),
            .user_id = settled_user_id,
            .flow_type = 2,
            .amount_cents = -res->total_fee_cents,
            .balance_before_cents = before_balance_cents,
            .balance_after_cents = res->new_balance_cents,
            .related_order_id = std::string(order_id),
            .operator_id = 0,
            .remark = "充电订单扣费",
            .idempotent_key = std::string(idempotent_key),
            .created_at = res->settled_at
        });
        RedisCache::instance().del_prefix("cache:dashboard:");
        RedisCache::instance().del_prefix("cache:station:");
        RedisCache::instance().del(std::format("cache:wallet:{}", settled_user_id));
        RedisCache::instance().del(std::format("cache:order:active:{}", settled_user_id));
    }
    return res;
}

Result<OrderListResponseData> DbRepository::get_user_orders_paged(
    int64_t user_id,
    int page,
    int page_size,
    std::string_view status_filter,
    std::string_view sort_order
) {
    auto conn = DbPool::instance().acquire_reader();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    int offset = (page - 1) * page_size;
    std::string where = std::format("WHERE o.user_id = {}", user_id);
    if (!status_filter.empty()) {
        where += std::format(" AND o.order_status = '{}'", status_filter);
    }

    std::string order_by = (sort_order == "asc") ? "ORDER BY o.created_at ASC" : "ORDER BY o.created_at DESC";

    std::string count_sql = std::format("SELECT COUNT(*) FROM charging_orders o {};", where);
    PgResultGuard count_res(conn->exec(count_sql.c_str()));
    int64_t total = count_res.is_ok() && count_res.rows() > 0 ? std::stoll(count_res.value(0, 0)) : 0;

    std::string sql = std::format(
        "SELECT o.order_id, o.station_id, s.station_name, o.pile_id, p.type, o.order_status, o.start_time, o.end_time, o.charged_energy_kwh, o.electricity_fee_cents, o.service_fee_cents, o.overtime_duration_minutes, o.overtime_fee_cents, o.total_fee_cents, o.settled_at "
        "FROM charging_orders o LEFT JOIN stations s ON o.station_id = s.station_id LEFT JOIN piles p ON o.pile_id = p.pile_id "
        "{} {} LIMIT {} OFFSET {};",
        where, order_by, page_size, offset
    );

    PgResultGuard res(conn->exec(sql.c_str()));
    if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);

    OrderListResponseData data;
    data.total = total;
    data.page = page;
    data.page_size = page_size;
    data.sort_order = std::string(sort_order);

    for (int i = 0; i < res.rows(); ++i) {
        int64_t st = std::stoll(res.value(i, 6));
        int64_t et = std::stoll(res.value(i, 7));
        int duration_mins = et > st ? static_cast<int>((et - st) / 60000) : 0;
        int64_t total_fee = std::stoll(res.value(i, 13));

        data.orders.push_back(OrderItemDTO{
            .order_id = res.value(i, 0),
            .station_id = std::stoll(res.value(i, 1)),
            .station_name = res.value(i, 2),
            .pile_id = res.value(i, 3),
            .pile_type = res.value(i, 4),
            .order_status = res.value(i, 5),
            .start_time = st,
            .end_time = et,
            .duration_minutes = duration_mins,
            .charged_energy_kwh = std::stod(res.value(i, 8)),
            .electricity_fee = cents_to_yuan(std::stoll(res.value(i, 9))),
            .service_fee = cents_to_yuan(std::stoll(res.value(i, 10))),
            .overtime_minutes = std::stoi(res.value(i, 11)),
            .overtime_fee = cents_to_yuan(std::stoll(res.value(i, 12))),
            .total_fee = cents_to_yuan(total_fee),
            .total_fee_cents = total_fee,
            .settled_at = std::stoll(res.value(i, 14))
        });
    }

    return data;
}

Result<OrderListResponseData> DbRepository::get_orders_admin_paged(
    int page,
    int page_size,
    int64_t station_id_filter,
    std::string_view status_filter,
    int64_t user_id_filter,
    int64_t start_date_ms,
    int64_t end_date_ms,
    std::string_view sort_order
) {
    auto conn = DbPool::instance().acquire_reader();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    int offset = (page - 1) * page_size;
    std::string where = "WHERE 1=1";
    if (station_id_filter > 0) {
        where += std::format(" AND o.station_id = {}", station_id_filter);
    }
    if (!status_filter.empty()) {
        std::string safe_st;
        for (char c : status_filter) {
            if (c == '\'') safe_st += "''";
            else safe_st += c;
        }
        where += std::format(" AND o.order_status = '{}'", safe_st);
    }
    if (user_id_filter > 0) {
        where += std::format(" AND o.user_id = {}", user_id_filter);
    }
    if (start_date_ms > 0) {
        where += std::format(" AND o.created_at >= {}", start_date_ms);
    }
    if (end_date_ms > 0) {
        where += std::format(" AND o.created_at <= {}", end_date_ms);
    }

    std::string sort_dir = (sort_order == "asc" || sort_order == "ASC") ? "ASC" : "DESC";

    bool has_filter = (station_id_filter > 0 || !status_filter.empty() || user_id_filter > 0 || start_date_ms > 0 || end_date_ms > 0);
    int64_t total = 0;
    if (!has_filter) {
        auto cached_cnt = RedisCache::instance().get("admin:orders:count:all");
        if (cached_cnt && !cached_cnt->empty()) {
            try {
                total = std::stoll(*cached_cnt);
            } catch (...) {
                total = 0;
            }
        }
    }

    if (total == 0 || has_filter) {
        std::string count_sql = std::format("SELECT COUNT(*) FROM charging_orders o {};", where);
        PgResultGuard count_res(conn->exec(count_sql.c_str()));
        total = count_res.is_ok() && count_res.rows() > 0 ? std::stoll(count_res.value(0, 0)) : 0;
        if (!has_filter && total > 0) {
            RedisCache::instance().set("admin:orders:count:all", std::to_string(total), 60);
        }
    }

    std::string sql = std::format(
        "SELECT po.order_id, po.user_id, COALESCE(u.phone, ''), po.station_id, COALESCE(s.station_name, ''), "
        "po.pile_id, COALESCE(p.type, 'FAST'), po.order_status, po.start_time, po.end_time, "
        "po.charged_energy_kwh, po.electricity_fee_cents, po.service_fee_cents, po.overtime_duration_minutes, "
        "po.overtime_fee_cents, po.total_fee_cents, po.settled_at "
        "FROM ("
        "  SELECT o.order_id, o.user_id, o.station_id, o.pile_id, o.order_status, o.start_time, o.end_time, "
        "         o.charged_energy_kwh, o.electricity_fee_cents, o.service_fee_cents, o.overtime_duration_minutes, "
        "         o.overtime_fee_cents, o.total_fee_cents, o.settled_at, o.created_at "
        "  FROM charging_orders o "
        "  {} "
        "  ORDER BY o.created_at {}, o.order_id {} "
        "  LIMIT {} OFFSET {}"
        ") po "
        "LEFT JOIN stations s ON po.station_id = s.station_id "
        "LEFT JOIN piles p ON po.pile_id = p.pile_id "
        "LEFT JOIN users u ON po.user_id = u.user_id "
        "ORDER BY po.created_at {}, po.order_id {};",
        where, sort_dir, sort_dir, page_size, offset, sort_dir, sort_dir
    );

    PgResultGuard res(conn->exec(sql.c_str()));
    if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);

    OrderListResponseData data;
    data.total = total;
    data.page = page;
    data.page_size = page_size;

    auto parse_ll = [&](int row, int col) -> int64_t {
        const char* v = res.value(row, col);
        return (v && *v) ? std::stoll(v) : 0LL;
    };
    auto parse_d = [&](int row, int col) -> double {
        const char* v = res.value(row, col);
        return (v && *v) ? std::stod(v) : 0.0;
    };
    auto parse_i = [&](int row, int col) -> int {
        const char* v = res.value(row, col);
        return (v && *v) ? std::stoi(v) : 0;
    };
    auto parse_str = [&](int row, int col) -> std::string {
        const char* v = res.value(row, col);
        return v ? std::string(v) : std::string();
    };

    for (int i = 0; i < res.rows(); ++i) {
        int64_t st = parse_ll(i, 8);
        int64_t et = parse_ll(i, 9);
        int duration_mins = et > st ? static_cast<int>((et - st) / 60000) : 0;
        int64_t total_fee = parse_ll(i, 15);

        data.orders.push_back(OrderItemDTO{
            .order_id = parse_str(i, 0),
            .user_id = parse_ll(i, 1),
            .user_phone = parse_str(i, 2),
            .station_id = parse_ll(i, 3),
            .station_name = parse_str(i, 4),
            .pile_id = parse_str(i, 5),
            .pile_type = parse_str(i, 6),
            .order_status = parse_str(i, 7),
            .start_time = st,
            .end_time = et,
            .duration_minutes = duration_mins,
            .charged_energy_kwh = parse_d(i, 10),
            .electricity_fee = cents_to_yuan(parse_ll(i, 11)),
            .service_fee = cents_to_yuan(parse_ll(i, 12)),
            .overtime_minutes = parse_i(i, 13),
            .overtime_fee = cents_to_yuan(parse_ll(i, 14)),
            .total_fee = cents_to_yuan(total_fee),
            .total_fee_cents = total_fee,
            .settled_at = parse_ll(i, 16)
        });
    }

    return data;
}

Result<AdminUserOrdersResponseData> DbRepository::get_admin_user_orders(
    int64_t user_id,
    int page,
    int page_size,
    std::string_view sort_order
) {
    auto u_res = get_user_by_id(user_id);
    if (!u_res) return std::unexpected(AppError::UserNotFound);

    auto o_res = get_user_orders_paged(user_id, page, page_size, "", sort_order);
    if (!o_res) return std::unexpected(o_res.error());

    return AdminUserOrdersResponseData{
        .user_id = user_id,
        .phone = u_res->phone,
        .nickname = u_res->nickname,
        .total = o_res->total,
        .page = o_res->page,
        .page_size = o_res->page_size,
        .sort_order = std::string(sort_order),
        .orders = std::move(o_res->orders)
    };
}

Result<AdminOrderRefundResponseData> DbRepository::refund_order_with_wallet(
    std::string_view order_id,
    int64_t refund_amount_cents,
    int64_t operator_id,
    std::string_view idempotent_key,
    std::string_view reason
) {
    auto res = DbPool::instance().with_transaction([&](DbConnection& conn) -> Result<AdminOrderRefundResponseData> {
        // 锁定订单行
        std::string o_sql = std::format("SELECT user_id, order_status, total_fee_cents FROM charging_orders WHERE order_id = '{}' FOR UPDATE;", order_id);
        PgResultGuard o_res(conn.exec(o_sql.c_str()));
        if (!o_res.is_ok() || o_res.rows() == 0) return std::unexpected(AppError::OrderNotFound);

        std::string st = o_res.value(0, 1);
        if (st == "REFUNDED") {
            return std::unexpected(AppError::OrderAlreadyRefunded);
        }

        int64_t user_id = std::stoll(o_res.value(0, 0));
        int64_t paid_total_cents = std::stoll(o_res.value(0, 2));
        if (refund_amount_cents <= 0 || refund_amount_cents > paid_total_cents) {
            return std::unexpected(AppError::InvalidRefundAmount);
        }

        // 锁定钱包行
        std::string w_sql = std::format("SELECT balance_cents FROM user_wallets WHERE user_id = {} FOR UPDATE;", user_id);
        PgResultGuard w_res(conn.exec(w_sql.c_str()));
        if (!w_res.is_ok() || w_res.rows() == 0) return std::unexpected(AppError::UserNotFound);

        int64_t before_cents = std::stoll(w_res.value(0, 0));
        int64_t after_cents = before_cents + refund_amount_cents;
        int64_t now = current_time_ms();
        std::string tx_id = std::format("TX_RF_{}_{}", now, user_id);

        // 钱包余额回补
        std::string u_wallet = std::format("UPDATE user_wallets SET balance_cents = {}, updated_at = {} WHERE user_id = {};", after_cents, now, user_id);
        conn.exec(u_wallet.c_str());

        // 订单状态置为 REFUNDED
        std::string u_order = std::format(
            "UPDATE charging_orders SET order_status = 'REFUNDED', refund_transaction_id = '{}', operator_id = {}, refund_reason = '{}', refunded_at = {}, updated_at = {} WHERE order_id = '{}';",
            tx_id, operator_id, reason, now, now, order_id
        );
        conn.exec(u_order.c_str());

        return AdminOrderRefundResponseData{
            .refund_transaction_id = tx_id,
            .order_id = std::string(order_id),
            .user_id = user_id,
            .refund_amount = cents_to_yuan(refund_amount_cents),
            .refund_amount_cents = refund_amount_cents,
            .user_balance_before = cents_to_yuan(before_cents),
            .user_balance_after = cents_to_yuan(after_cents),
            .order_status = "REFUNDED",
            .operator_id = operator_id,
            .refunded_at = now
        };
    });

    if (res) {
        AsyncFlowPersister::instance().enqueue(WalletFlowItem{
            .id = res->refund_transaction_id,
            .user_id = res->user_id,
            .flow_type = 3,
            .amount_cents = res->refund_amount_cents,
            .balance_before_cents = yuan_to_cents(res->user_balance_before),
            .balance_after_cents = yuan_to_cents(res->user_balance_after),
            .related_order_id = std::string(order_id),
            .operator_id = operator_id,
            .remark = std::string(reason),
            .idempotent_key = std::string(idempotent_key),
            .created_at = res->refunded_at
        });
        RedisCache::instance().del_prefix("cache:dashboard:");
        RedisCache::instance().del_prefix("cache:station:");
        RedisCache::instance().del(std::format("cache:wallet:{}", res->user_id));
    }
    return res;
}

// ==========================================
// 6. 运营大盘数据统计
// ==========================================

Result<AdminDashboardSummaryData> DbRepository::get_admin_dashboard_summary() {
    auto conn = DbPool::instance().acquire_reader();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    int64_t now = current_time_ms();
    int64_t cst_offset = 8 * 3600 * 1000LL;
    int64_t today_start = ((now + cst_offset) / 86400000LL) * 86400000LL - cst_offset;
    int64_t month_start = today_start - 29LL * 86400000LL;

    // 今日/本月/全表总营收与充电量
    std::string sql = std::format(
        "SELECT "
        "COALESCE(SUM(CASE WHEN created_at >= {} THEN total_fee_cents ELSE 0 END), 0) as today_rev, "
        "COALESCE(SUM(CASE WHEN created_at >= {} THEN total_fee_cents ELSE 0 END), 0) as month_rev, "
        "COALESCE(SUM(total_fee_cents), 0) as total_rev, "
        "COALESCE(SUM(CASE WHEN created_at >= {} THEN charged_energy_kwh ELSE 0 END), 0.0) as today_kwh, "
        "COUNT(CASE WHEN created_at >= {} THEN 1 END) as today_orders "
        "FROM charging_orders WHERE order_status IN ('COMPLETED', 'UNSETTLED');",
        today_start, month_start, today_start, today_start
    );

    PgResultGuard res(conn->exec(sql.c_str()));
    if (!res.is_ok() || res.rows() == 0) return std::unexpected(AppError::DatabaseError);

    // 用户总数
    PgResultGuard u_res(conn->exec("SELECT COUNT(*) FROM users;"));
    int64_t total_users = (u_res.is_ok() && u_res.rows() > 0) ? std::stoll(u_res.value(0, 0)) : 0;

    int64_t today_cents = std::stoll(res.value(0, 0));
    int64_t month_cents = std::stoll(res.value(0, 1));
    int64_t orders_cents = std::stoll(res.value(0, 2));

    // 全盘历史累计基底 (远大于当月数据)
    int64_t hist_cents = get_platform_metric("historical_revenue_cents", 8865000000LL);
    int64_t total_cents = hist_cents + orders_cents;

    // 活跃充电会话直接取自内存池最新状态
    int64_t active_sessions = ChargingStatePool::instance().get_pile_status_overview().in_use_count;

    return AdminDashboardSummaryData{
        .today_revenue = cents_to_yuan(today_cents),
        .today_revenue_cents = today_cents,
        .month_revenue = cents_to_yuan(month_cents),
        .month_revenue_cents = month_cents,
        .total_revenue = cents_to_yuan(total_cents),
        .total_revenue_cents = total_cents,
        .today_energy_kwh = std::stod(res.value(0, 3)),
        .today_order_count = std::stoll(res.value(0, 4)),
        .total_user_count = total_users,
        .active_charging_sessions = active_sessions
    };
}

Result<AdminRevenueTrendData> DbRepository::get_admin_revenue_trend(int days) {
    auto conn = DbPool::instance().acquire_reader();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    int64_t now = current_time_ms();
    int64_t cst_offset = 8 * 3600 * 1000LL;
    int64_t today_start = ((now + cst_offset) / 86400000LL) * 86400000LL - cst_offset;
    int64_t start_threshold = today_start - static_cast<int64_t>(days - 1) * 86400000LL;

    AdminRevenueTrendData data;
    data.time_range = (days == 7 ? "LAST_7_DAYS" : "LAST_30_DAYS");
    data.dates.reserve(days);
    data.revenue_series.assign(days, 0.0);
    data.energy_kwh_series.assign(days, 0.0);
    data.order_count_series.assign(days, 0);

    for (int d = 0; d < days; ++d) {
        int64_t day_ms = start_threshold + static_cast<int64_t>(d) * 86400000LL;
        time_t t = day_ms / 1000;
        struct tm* tm_info = localtime(&t);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d", tm_info);
        data.dates.push_back(buf);
    }

    // 单次高效 SQL 聚合查询，彻底消除原本 30 次循环串行开销
    std::string sql = std::format(
        "SELECT ((created_at - {}) / 86400000) as day_idx, "
        "COALESCE(SUM(total_fee_cents), 0), COALESCE(SUM(charged_energy_kwh), 0.0), COUNT(*) "
        "FROM charging_orders "
        "WHERE created_at >= {} AND created_at < {} "
        "  AND order_status IN ('COMPLETED', 'UNSETTLED') "
        "GROUP BY day_idx;",
        start_threshold, start_threshold, today_start + 86400000LL
    );

    PgResultGuard res(conn->exec(sql.c_str()));
    if (res.is_ok()) {
        for (int r = 0; r < res.rows(); ++r) {
            int64_t d_idx = std::stoll(res.value(r, 0));
            if (d_idx >= 0 && d_idx < days) {
                int64_t rev_cents = std::stoll(res.value(r, 1));
                double energy_kwh = std::stod(res.value(r, 2));
                int64_t ord_count = std::stoll(res.value(r, 3));
                data.revenue_series[d_idx] = cents_to_yuan(rev_cents);
                data.energy_kwh_series[d_idx] = energy_kwh;
                data.order_count_series[d_idx] = ord_count;
            }
        }
    }

    return data;
}

Result<AdminPileStatusOverviewData> DbRepository::get_admin_pile_status_overview() {
    return ChargingStatePool::instance().get_pile_status_overview();
}

// ==========================================
// 7. 充电桩预约
// ==========================================

Result<ReservePileResponseData> DbRepository::create_reservation(int64_t user_id, std::string_view pile_id) {
    int64_t now = current_time_ms();
    int64_t expire_at = now + 120000; // 2 分钟 (120s)
    std::string res_id = std::format("RES_{}_{}", now, user_id);

    return DbPool::instance().with_transaction([&](DbConnection& conn) -> Result<ReservePileResponseData> {
        // 1. 检查用户是否有尚未结束且未超时的活跃预约
        std::string check_user_sql = std::format(
            "SELECT reservation_id FROM pile_reservations WHERE user_id = {} AND status = 'ACTIVE' AND expire_at > {};",
            user_id, now
        );
        PgResultGuard u_res(conn.exec(check_user_sql.c_str()));
        if (!u_res.is_ok()) {
            return std::unexpected(AppError::DatabaseError);
        }
        if (u_res.rows() > 0) {
            return std::unexpected(AppError::ActiveOrderExists);
        }

        // 2. 检查电桩是否存在以及所属电站
        std::string pid_str = std::string(pile_id);
        std::string pile_sql = std::format(
            "SELECT p.station_id, COALESCE(s.station_name, ''), COALESCE(s.status, 1), p.pile_id, p.pile_name "
            "FROM piles p LEFT JOIN stations s ON p.station_id = s.station_id "
            "WHERE p.pile_id = '{}' OR LOWER(p.pile_id) = LOWER('{}') LIMIT 1;",
            pid_str, pid_str
        );
        PgResultGuard p_res(conn.exec(pile_sql.c_str()));
        if (!p_res.is_ok()) {
            return std::unexpected(AppError::DatabaseError);
        }
        if (p_res.rows() == 0) {
            return std::unexpected(AppError::ChargingPileNotFound);
        }
        int64_t st_id = std::stoll(p_res.value(0, 0));
        std::string st_name = p_res.value(0, 1);
        int s_status = std::stoi(p_res.value(0, 2));
        std::string canonical_pid = p_res.value(0, 3);
        std::string p_name = p_res.value(0, 4);

        if (st_name.empty()) {
            const StaticStation* s_static = find_static_station(static_cast<int32_t>(st_id));
            if (s_static) {
                st_name = s_static->name;
            } else {
                st_name = p_name;
            }
        }

        if (s_status == 2 || !StationStatusManager::instance().is_online(st_id)) {
            return std::unexpected(AppError::StationNotFound);
        }

        // 3. 锁定钱包并检查余额 (需要 >= 20.00元, 即 2000分)
        std::string w_sql = std::format("SELECT balance_cents, status FROM user_wallets WHERE user_id = {} FOR UPDATE;", user_id);
        PgResultGuard w_res(conn.exec(w_sql.c_str()));
        if (!w_res.is_ok()) return std::unexpected(AppError::DatabaseError);
        if (w_res.rows() == 0) return std::unexpected(AppError::UserNotFound);
        if (std::stoi(w_res.value(0, 1)) == 2) return std::unexpected(AppError::UserAccountFrozen);

        int64_t before_balance = std::stoll(w_res.value(0, 0));
        if (before_balance < 2000) {
            return std::unexpected(AppError::InsufficientBalance);
        }

        int64_t after_balance = before_balance - 2000;

        // 4. 扣除 20元 押金
        std::string update_wallet = std::format(
            "UPDATE user_wallets SET balance_cents = {}, updated_at = {} WHERE user_id = {};",
            after_balance, now, user_id
        );
        PgResultGuard uw_res(conn.exec(update_wallet.c_str()));
        if (!uw_res.is_ok()) return std::unexpected(AppError::DatabaseError);

        // 5. 插入预约单
        std::string insert_res = std::format(
            "INSERT INTO pile_reservations (reservation_id, user_id, station_id, pile_id, deposit_cents, penalty_fee_cents, refund_amount_cents, status, created_at, expire_at, fulfilled_at, cancelled_at, updated_at) "
            "VALUES ('{}', {}, {}, '{}', 2000, 0, 0, 'ACTIVE', {}, {}, 0, 0, {});",
            res_id, user_id, st_id, canonical_pid, now, expire_at, now
        );
        PgResultGuard ir_res(conn.exec(insert_res.c_str()));
        if (!ir_res.is_ok()) return std::unexpected(AppError::DatabaseError);

        // 7. 记录财务流水
        std::string tx_id = std::format("TX_RES_DEP_{}_{}", now, user_id);
        std::string insert_flow = std::format(
            "INSERT INTO wallet_transaction_flows (id, user_id, flow_type, amount_cents, balance_before_cents, balance_after_cents, related_order_id, operator_id, remark, idempotent_key, created_at) "
            "VALUES ('{}', {}, 5, -2000, {}, {}, '{}', 0, '预约充电桩押金支付', 'IDEM_{}', {});",
            tx_id, user_id, before_balance, after_balance, res_id, tx_id, now
        );
        PgResultGuard if_res(conn.exec(insert_flow.c_str()));
        if (!if_res.is_ok()) return std::unexpected(AppError::DatabaseError);

        RedisCache::instance().del(std::format("cache:wallet:{}", user_id));
        RedisCache::instance().del_prefix("cache:dashboard:");

        return ReservePileResponseData{
            .reservation_id = res_id,
            .pile_id = canonical_pid,
            .station_id = st_id,
            .station_name = st_name,
            .deposit = 20.0,
            .deposit_cents = 2000,
            .status = "ACTIVE",
            .created_at = now,
            .expire_at = expire_at,
            .timeout_seconds = 120,
            .wallet_balance = cents_to_yuan(after_balance),
            .wallet_balance_cents = after_balance
        };
    });
}

Result<std::optional<ReservationModel>> DbRepository::get_active_reservation_by_user(int64_t user_id) {
    auto conn = DbPool::instance().acquire_reader();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    int64_t now = current_time_ms();
    std::string sql = std::format(
        "SELECT reservation_id, user_id, station_id, pile_id, deposit_cents, penalty_fee_cents, refund_amount_cents, status, created_at, expire_at, fulfilled_at, cancelled_at, updated_at "
        "FROM pile_reservations WHERE user_id = {} AND status = 'ACTIVE' AND expire_at > {} ORDER BY created_at DESC LIMIT 1;",
        user_id, now
    );
    PgResultGuard res(conn->exec(sql.c_str()));
    if (!res.is_ok() || res.rows() == 0) {
        return std::optional<ReservationModel>{std::nullopt};
    }

    return std::optional<ReservationModel>{ReservationModel{
        .reservation_id = res.value(0, 0),
        .user_id = std::stoll(res.value(0, 1)),
        .station_id = std::stoll(res.value(0, 2)),
        .pile_id = res.value(0, 3),
        .deposit_cents = std::stoll(res.value(0, 4)),
        .penalty_fee_cents = std::stoll(res.value(0, 5)),
        .refund_amount_cents = std::stoll(res.value(0, 6)),
        .status = res.value(0, 7),
        .created_at = std::stoll(res.value(0, 8)),
        .expire_at = std::stoll(res.value(0, 9)),
        .fulfilled_at = std::stoll(res.value(0, 10)),
        .cancelled_at = std::stoll(res.value(0, 11)),
        .updated_at = std::stoll(res.value(0, 12))
    }};
}

Result<std::optional<ReservationModel>> DbRepository::get_active_reservation_by_pile(std::string_view pile_id) {
    auto conn = DbPool::instance().acquire_reader();
    if (!conn) return std::unexpected(AppError::DatabaseError);

    int64_t now = current_time_ms();
    std::string sql = std::format(
        "SELECT reservation_id, user_id, station_id, pile_id, deposit_cents, penalty_fee_cents, refund_amount_cents, status, created_at, expire_at, fulfilled_at, cancelled_at, updated_at "
        "FROM pile_reservations WHERE pile_id = '{}' AND status = 'ACTIVE' AND expire_at > {} ORDER BY created_at DESC LIMIT 1;",
        pile_id, now
    );
    PgResultGuard res(conn->exec(sql.c_str()));
    if (!res.is_ok() || res.rows() == 0) {
        return std::optional<ReservationModel>{std::nullopt};
    }

    return std::optional<ReservationModel>{ReservationModel{
        .reservation_id = res.value(0, 0),
        .user_id = std::stoll(res.value(0, 1)),
        .station_id = std::stoll(res.value(0, 2)),
        .pile_id = res.value(0, 3),
        .deposit_cents = std::stoll(res.value(0, 4)),
        .penalty_fee_cents = std::stoll(res.value(0, 5)),
        .refund_amount_cents = std::stoll(res.value(0, 6)),
        .status = res.value(0, 7),
        .created_at = std::stoll(res.value(0, 8)),
        .expire_at = std::stoll(res.value(0, 9)),
        .fulfilled_at = std::stoll(res.value(0, 10)),
        .cancelled_at = std::stoll(res.value(0, 11)),
        .updated_at = std::stoll(res.value(0, 12))
    }};
}

Result<CancelReservationResponseData> DbRepository::cancel_reservation(int64_t user_id, std::string_view reservation_id) {
    int64_t now = current_time_ms();

    return DbPool::instance().with_transaction([&](DbConnection& conn) -> Result<CancelReservationResponseData> {
        std::string sql;
        if (reservation_id.empty()) {
            sql = std::format(
                "SELECT reservation_id, pile_id, deposit_cents, status, expire_at FROM pile_reservations "
                "WHERE user_id = {} AND status = 'ACTIVE' AND expire_at > {} ORDER BY created_at DESC LIMIT 1 FOR UPDATE;",
                user_id, now
            );
        } else {
            sql = std::format(
                "SELECT reservation_id, pile_id, deposit_cents, status, expire_at FROM pile_reservations "
                "WHERE reservation_id = '{}' AND user_id = {} FOR UPDATE;",
                reservation_id, user_id
            );
        }

        PgResultGuard res(conn.exec(sql.c_str()));
        if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);
        if (res.rows() == 0) {
            return std::unexpected(AppError::NoActiveOrderFound);
        }

        std::string res_id = res.value(0, 0);
        std::string pile_id = res.value(0, 1);
        int64_t deposit_cents = std::stoll(res.value(0, 2));
        std::string status = res.value(0, 3);
        int64_t expire_at = std::stoll(res.value(0, 4));

        if (status != "ACTIVE" || now >= expire_at) {
            return std::unexpected(AppError::OrderCannotBeStopped);
        }

        // 扣除 5 元手续费 (500分)，退还 15 元 (1500分)
        int64_t penalty_cents = 500;
        int64_t refund_cents = deposit_cents - penalty_cents; // 1500

        // 锁定钱包退款
        std::string w_sql = std::format("SELECT balance_cents FROM user_wallets WHERE user_id = {} FOR UPDATE;", user_id);
        PgResultGuard w_res(conn.exec(w_sql.c_str()));
        if (!w_res.is_ok() || w_res.rows() == 0) return std::unexpected(AppError::UserNotFound);

        int64_t before_cents = std::stoll(w_res.value(0, 0));
        int64_t after_cents = before_cents + refund_cents;

        std::string u_wallet = std::format(
            "UPDATE user_wallets SET balance_cents = {}, updated_at = {} WHERE user_id = {};",
            after_cents, now, user_id
        );
        conn.exec(u_wallet.c_str());

        // 更新预约单状态为 CANCELLED
        std::string u_res_sql = std::format(
            "UPDATE pile_reservations SET status = 'CANCELLED', penalty_fee_cents = {}, refund_amount_cents = {}, cancelled_at = {}, updated_at = {} WHERE reservation_id = '{}';",
            penalty_cents, refund_cents, now, now, res_id
        );
        conn.exec(u_res_sql.c_str());

        // 记录退款流水
        std::string tx_id = std::format("TX_RES_REF_{}_{}", now, user_id);
        std::string insert_flow = std::format(
            "INSERT INTO wallet_transaction_flows (id, user_id, flow_type, amount_cents, balance_before_cents, balance_after_cents, related_order_id, operator_id, remark, idempotent_key, created_at) "
            "VALUES ('{}', {}, 6, {}, {}, {}, '{}', 0, '取消预约押金退还(扣除5元手续费)', 'IDEM_{}', {});",
            tx_id, user_id, refund_cents, before_cents, after_cents, res_id, tx_id, now
        );
        conn.exec(insert_flow.c_str());

        RedisCache::instance().del(std::format("cache:wallet:{}", user_id));
        RedisCache::instance().del_prefix("cache:dashboard:");

        return CancelReservationResponseData{
            .reservation_id = res_id,
            .pile_id = pile_id,
            .status = "CANCELLED",
            .deposit = cents_to_yuan(deposit_cents),
            .deposit_cents = deposit_cents,
            .penalty_fee = cents_to_yuan(penalty_cents),
            .penalty_fee_cents = penalty_cents,
            .refund_amount = cents_to_yuan(refund_cents),
            .refund_amount_cents = refund_cents,
            .new_balance = cents_to_yuan(after_cents),
            .new_balance_cents = after_cents,
            .cancelled_at = now
        };
    });
}

Result<void> DbRepository::fulfill_reservation(int64_t user_id, std::string_view pile_id) {
    int64_t now = current_time_ms();

    return DbPool::instance().with_transaction([&](DbConnection& conn) -> Result<void> {
        std::string pid_str = std::string(pile_id);
        std::string sql = std::format(
            "SELECT reservation_id, deposit_cents FROM pile_reservations "
            "WHERE user_id = {} AND (pile_id = '{}' OR LOWER(pile_id) = LOWER('{}')) AND status = 'ACTIVE' AND expire_at > {} ORDER BY created_at DESC LIMIT 1 FOR UPDATE;",
            user_id, pid_str, pid_str, now
        );
        PgResultGuard res(conn.exec(sql.c_str()));
        if (!res.is_ok()) return std::unexpected(AppError::DatabaseError);
        if (res.rows() == 0) {
            return {}; // 没有活跃预约单，无需履约退押金
        }

        std::string res_id = res.value(0, 0);
        int64_t deposit_cents = std::stoll(res.value(0, 1)); // 2000

        // 全额退还 20 元押金
        std::string w_sql = std::format("SELECT balance_cents FROM user_wallets WHERE user_id = {} FOR UPDATE;", user_id);
        PgResultGuard w_res(conn.exec(w_sql.c_str()));
        if (!w_res.is_ok() || w_res.rows() == 0) return std::unexpected(AppError::UserNotFound);

        int64_t before_cents = std::stoll(w_res.value(0, 0));
        int64_t after_cents = before_cents + deposit_cents;

        std::string u_wallet = std::format(
            "UPDATE user_wallets SET balance_cents = {}, updated_at = {} WHERE user_id = {};",
            after_cents, now, user_id
        );
        conn.exec(u_wallet.c_str());

        // 更新预约单为 FULFILLED
        std::string u_res_sql = std::format(
            "UPDATE pile_reservations SET status = 'FULFILLED', refund_amount_cents = {}, fulfilled_at = {}, updated_at = {} WHERE reservation_id = '{}';",
            deposit_cents, now, now, res_id
        );
        conn.exec(u_res_sql.c_str());

        // 记录流水
        std::string tx_id = std::format("TX_RES_FUL_{}_{}", now, user_id);
        std::string insert_flow = std::format(
            "INSERT INTO wallet_transaction_flows (id, user_id, flow_type, amount_cents, balance_before_cents, balance_after_cents, related_order_id, operator_id, remark, idempotent_key, created_at) "
            "VALUES ('{}', {}, 6, {}, {}, {}, '{}', 0, '预约到场充电押金全额退还', 'IDEM_{}', {});",
            tx_id, user_id, deposit_cents, before_cents, after_cents, res_id, tx_id, now
        );
        conn.exec(insert_flow.c_str());

        RedisCache::instance().del(std::format("cache:wallet:{}", user_id));
        RedisCache::instance().del_prefix("cache:dashboard:");
        return {};
    });
}

Result<std::vector<std::string>> DbRepository::timeout_expired_reservations() {
    int64_t now = current_time_ms();
    std::vector<std::string> expired_piles;

    auto res = DbPool::instance().with_transaction([&](DbConnection& conn) -> Result<std::vector<std::string>> {
        std::string sql = std::format(
            "SELECT reservation_id, pile_id, user_id FROM pile_reservations "
            "WHERE status = 'ACTIVE' AND expire_at <= {} FOR UPDATE;",
            now
        );
        PgResultGuard q_res(conn.exec(sql.c_str()));
        if (!q_res.is_ok() || q_res.rows() == 0) {
            return expired_piles;
        }

        for (int i = 0; i < q_res.rows(); ++i) {
            std::string res_id = q_res.value(i, 0);
            std::string pile_id = q_res.value(i, 1);
            expired_piles.push_back(pile_id);

            // 标记为 TIMEOUT，没收全部押金 (penalty_fee_cents = 2000, refund = 0)
            std::string u_res = std::format(
                "UPDATE pile_reservations SET status = 'TIMEOUT', penalty_fee_cents = 2000, refund_amount_cents = 0, updated_at = {} WHERE reservation_id = '{}';",
                now, res_id
            );
            conn.exec(u_res.c_str());
        }

        return expired_piles;
    });

    return res;
}

// ==========================================
// 8. 平台全盘指标与每日跨天模拟
// ==========================================

int64_t DbRepository::get_platform_metric(std::string_view key, int64_t default_val) {
    auto conn = DbPool::instance().acquire_reader();
    if (!conn) return default_val;
    std::string sql = std::format("SELECT metric_val FROM platform_metrics WHERE metric_key = '{}';", key);
    PgResultGuard res(conn->exec(sql.c_str()));
    if (res.is_ok() && res.rows() > 0) {
        return std::stoll(res.value(0, 0));
    }
    return default_val;
}

void DbRepository::set_platform_metric(std::string_view key, int64_t val) {
    auto conn = DbPool::instance().acquire();
    if (!conn) return;
    int64_t now = current_time_ms();
    std::string sql = std::format(
        "INSERT INTO platform_metrics (metric_key, metric_val, updated_at) VALUES ('{}', {}, {}) "
        "ON CONFLICT (metric_key) DO UPDATE SET metric_val = EXCLUDED.metric_val, updated_at = EXCLUDED.updated_at;",
        key, val, now
    );
    conn->exec(sql.c_str());
}

void DbRepository::add_platform_metric(std::string_view key, int64_t delta) {
    auto conn = DbPool::instance().acquire();
    if (!conn) return;
    int64_t now = current_time_ms();
    std::string sql = std::format(
        "INSERT INTO platform_metrics (metric_key, metric_val, updated_at) VALUES ('{}', {}, {}) "
        "ON CONFLICT (metric_key) DO UPDATE SET metric_val = platform_metrics.metric_val + EXCLUDED.metric_val, updated_at = EXCLUDED.updated_at;",
        key, delta, now
    );
    conn->exec(sql.c_str());
}

void DbRepository::prune_orders_older_than_30_days() {
    auto conn = DbPool::instance().acquire();
    if (!conn) return;
    int64_t now = current_time_ms();
    int64_t cutoff = now - 30LL * 86400000LL;

    // 1. 统计即将被淘汰的历史订单营收总和，沉淀到历史总营收基底中
    std::string sum_sql = std::format(
        "SELECT COALESCE(SUM(total_fee_cents), 0) FROM charging_orders WHERE created_at < {} AND order_status IN ('COMPLETED', 'UNSETTLED');",
        cutoff
    );
    PgResultGuard res(conn->exec(sum_sql.c_str()));
    if (res.is_ok() && res.rows() > 0) {
        int64_t deleted_cents = std::stoll(res.value(0, 0));
        if (deleted_cents > 0) {
            add_platform_metric("historical_revenue_cents", deleted_cents);
        }
    }

    // 2. 执行物理删除，确保数据库仅保留 30 天内的订单
    std::string del_sql = std::format("DELETE FROM charging_orders WHERE created_at < {};", cutoff);
    conn->exec(del_sql.c_str());
}

void DbRepository::generate_orders_for_day(int64_t day_idx) {
    int64_t now = current_time_ms();
    int64_t cst_offset = 8 * 3600 * 1000LL;
    int64_t day_start_ms = day_idx * 86400000LL - cst_offset;

    int64_t total_users = 20000;
    {
        auto conn = DbPool::instance().acquire_reader();
        if (conn) {
            PgResultGuard u_res(conn->exec("SELECT COUNT(*) FROM users;"));
            if (u_res.is_ok() && u_res.rows() > 0) {
                total_users = std::max<int64_t>(1, std::stoll(u_res.value(0, 0)));
            }
        }
    }

    constexpr int ORDER_BATCH = 2000;
    std::string o_sql;
    o_sql.reserve(1024 * 512);
    int batch_count = 0;

    auto flush_batch = [&]() {
        if (batch_count == 0) return;
        o_sql += " ON CONFLICT (order_id) DO NOTHING;";
        auto conn = DbPool::instance().acquire();
        if (conn) {
            conn->exec(o_sql.c_str());
        }
        o_sql.clear();
        batch_count = 0;
    };

    for (size_t i = 0; i < STATIC_STATION_COUNT; ++i) {
        int64_t sid = STATIC_STATIONS[i].station_id;
        if (!StationStatusManager::instance().is_online(sid)) continue;

        auto p_list = ChargingStatePool::instance().get_piles_by_station(sid);
        if (p_list.empty()) continue;

        double elec_price = 1.15 + static_cast<double>((static_cast<uint64_t>(sid) * 104729ULL + 12345ULL) % 71) * 0.01;
        double serv_price = 0.35;

        uint64_t seed = (static_cast<uint64_t>(sid) * 314159ULL) ^ (static_cast<uint64_t>(day_idx) * 271828ULL);
        int station_tier = static_cast<int>((static_cast<uint64_t>(sid) * 1337ULL) % 100);
        int orders_today = 0;
        if (station_tier < 15) {
            orders_today = 1 + static_cast<int>(seed % 4);
        } else if (station_tier < 70) {
            orders_today = (seed % 3 == 0) ? 0 : (1 + static_cast<int>(seed % 2));
        } else {
            orders_today = (seed % 2 == 0) ? 0 : 1;
        }

        for (int k = 0; k < orders_today; ++k) {
            uint64_t ord_seed = seed ^ (static_cast<uint64_t>(k) * 65537ULL);
            const auto& selected_pile = p_list[ord_seed % p_list.size()];
            bool is_fast = (selected_pile.type == "FAST");

            int hour = (ord_seed % 100 < 75) ? (8 + static_cast<int>(ord_seed % 14)) : static_cast<int>(ord_seed % 24);
            int minute = static_cast<int>((ord_seed / 24) % 60);
            int second = static_cast<int>((ord_seed / 1440) % 60);
            int64_t start_time = day_start_ms + (hour * 3600LL + minute * 60LL + second) * 1000LL;
            if (start_time > now) start_time = now - 1800000LL;

            int dur_mins = is_fast ? (30 + static_cast<int>(ord_seed % 45)) : (120 + static_cast<int>(ord_seed % 240));
            int64_t end_time = start_time + dur_mins * 60 * 1000LL;
            if (end_time > now) end_time = now;

            int start_soc = 15 + static_cast<int>(ord_seed % 25);
            int end_soc = 85 + static_cast<int>(ord_seed % 15);
            if (end_soc > 100) end_soc = 100;

            double battery_cap = 50.0 + static_cast<double>(ord_seed % 35);
            double charged_energy = std::round(battery_cap * (end_soc - start_soc) / 100.0 * 100.0) / 100.0;
            int64_t elec_cents = static_cast<int64_t>(charged_energy * elec_price * 100.0);
            int64_t serv_cents = static_cast<int64_t>(charged_energy * serv_price * 100.0);
            int overtime_mins = (ord_seed % 100 < 12) ? (15 + static_cast<int>(ord_seed % 4) * 15) : 0;
            int64_t overtime_cents = (overtime_mins / 15) * 500LL;
            int64_t total_cents = elec_cents + serv_cents + overtime_cents;

            int64_t uid = 1 + static_cast<int64_t>(ord_seed % total_users);
            std::string st_status = (ord_seed % 100 < 97) ? "COMPLETED" : "UNSETTLED";
            std::string ord_id = std::format("ORD_{}_{:05d}_{:02d}", start_time, sid, k);
            std::string stop_rsn = (ord_seed % 2 == 0) ? "USER_MANUAL_STOP" : "TARGET_SOC_REACHED";
            int64_t settled_at = (st_status == "COMPLETED") ? end_time : 0;

            if (batch_count == 0) {
                o_sql = "INSERT INTO charging_orders (order_id, user_id, station_id, pile_id, strategy_type, strategy_value, order_status, start_time, end_time, start_soc, end_soc, charged_energy_kwh, electricity_price, electricity_fee_cents, service_price, service_fee_cents, overtime_grace_minutes, overtime_duration_minutes, overtime_rate_per_15min, overtime_fee_cents, total_fee_cents, stop_reason, settled_at, created_at, updated_at) VALUES ";
            } else {
                o_sql += ", ";
            }

            o_sql += std::format("('{}', {}, {}, '{}', 'FULL', 0.0, '{}', {}, {}, {}, {}, {:.2f}, {:.2f}, {}, {:.2f}, {}, 15, {}, 5.00, {}, {}, '{}', {}, {}, {})",
                                 sql_escape(ord_id), uid, sid, sql_escape(selected_pile.pile_id),
                                 st_status, start_time, end_time, start_soc, end_soc, charged_energy,
                                 elec_price, elec_cents, serv_price, serv_cents,
                                 overtime_mins, overtime_cents, total_cents,
                                 stop_rsn, settled_at, start_time, end_time);

            batch_count++;
            if (batch_count >= ORDER_BATCH) {
                flush_batch();
            }
        }
    }
    flush_batch();
}

void DbRepository::check_and_simulate_daily_orders() {
    int64_t now = current_time_ms();
    int64_t cst_offset = 8 * 3600 * 1000LL;
    int64_t current_day = (now + cst_offset) / 86400000LL;

    static std::atomic<int64_t> cached_last_day{0};
    if (cached_last_day.load(std::memory_order_relaxed) >= current_day) {
        return;
    }

    int64_t last_day = get_platform_metric("last_simulated_day", 0);
    if (last_day == 0) {
        set_platform_metric("last_simulated_day", current_day);
        cached_last_day.store(current_day, std::memory_order_relaxed);
        return;
    }
    cached_last_day.store(last_day, std::memory_order_relaxed);

    if (current_day > last_day) {
        std::cout << "[Simulator] >>> Detected day transition: last_simulated_day=" << last_day 
                  << ", current_day=" << current_day << ". Generating new daily orders...\n";
        int64_t start_fill_day = std::max(last_day + 1, current_day - 29);
        for (int64_t d = start_fill_day; d <= current_day; ++d) {
            generate_orders_for_day(d);
        }

        prune_orders_older_than_30_days();
        set_platform_metric("last_simulated_day", current_day);
        cached_last_day.store(current_day, std::memory_order_relaxed);

        RedisCache::instance().del_prefix("cache:dashboard:");
        RedisCache::instance().del_prefix("cache:station:");
        std::cout << "[Simulator] >>> Completed daily simulation and pruned orders older than 30 days.\n";
    }
}

} // namespace ev

