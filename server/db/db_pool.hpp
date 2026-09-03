#pragma once

#include <libpq-fe.h>
#include <string>
#include <memory>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <expected>
#include <functional>
#include <unordered_set>
#include "../common/error.hpp"

namespace ev {

class DbConnection {
public:
    explicit DbConnection(const std::string& conninfo);
    ~DbConnection();

    DbConnection(const DbConnection&) = delete;
    DbConnection& operator=(const DbConnection&) = delete;

    bool is_valid() const;
    void reset();
    PGconn* raw() const { return conn_; }

    // 执行参数化 SQL
    PGresult* exec_params(
        const char* command,
        int nParams,
        const Oid* paramTypes,
        const char* const* paramValues,
        const int* paramLengths,
        const int* paramFormats,
        int resultFormat
    );

    // 简单 SQL 执行
    PGresult* exec(const char* query);

    // 预编译 SQL 支持
    bool prepare(const char* stmt_name, const char* query, int nParams = 0);
    PGresult* exec_prepared(
        const char* stmt_name,
        int nParams,
        const char* const* paramValues,
        const int* paramLengths = nullptr,
        const int* paramFormats = nullptr,
        int resultFormat = 0
    );

    std::string last_error() const;

private:
    std::string conninfo_;
    PGconn* conn_{nullptr};
    std::unordered_set<std::string> prepared_stmts_;
};

// RAII 结果封装
class PgResultGuard {
public:
    explicit PgResultGuard(PGresult* res) : res_(res) {}
    ~PgResultGuard() {
        if (res_) {
            PQclear(res_);
        }
    }

    PgResultGuard(const PgResultGuard&) = delete;
    PgResultGuard& operator=(const PgResultGuard&) = delete;

    PgResultGuard(PgResultGuard&& other) noexcept : res_(other.res_) {
        other.res_ = nullptr;
    }

    PgResultGuard& operator=(PgResultGuard&& other) noexcept {
        if (this != &other) {
            if (res_) PQclear(res_);
            res_ = other.res_;
            other.res_ = nullptr;
        }
        return *this;
    }

    PGresult* get() const { return res_; }
    PGresult* operator->() const { return res_; }
    explicit operator bool() const { return res_ != nullptr; }

    ExecStatusType status() const {
        return res_ ? PQresultStatus(res_) : PGRES_FATAL_ERROR;
    }

    bool is_ok() const {
        auto s = status();
        return s == PGRES_COMMAND_OK || s == PGRES_TUPLES_OK;
    }

    int rows() const { return res_ ? PQntuples(res_) : 0; }
    int cols() const { return res_ ? PQnfields(res_) : 0; }

    const char* value(int row, int col) const {
        return PQgetvalue(res_, row, col);
    }

    bool is_null(int row, int col) const {
        return PQgetisnull(res_, row, col) != 0;
    }

private:
    PGresult* res_{nullptr};
};

// 读写分离角色定义
enum class DbRole {
    Writer, // 主库连接 (写操作、事务修改、行级锁)
    Reader  // 从库连接 (只读副本、空间查询、报表与分页检索)
};

class DbPool {
public:
    using PooledConnection = std::unique_ptr<DbConnection, std::function<void(DbConnection*)>>;

    static DbPool& instance();

    // 读写分离双池初始化
    void init(
        const std::string& writer_conninfo,
        const std::string& reader_conninfo,
        size_t min_connections = 4,
        size_t max_connections = 32
    );

    // 单连接串兼容初始化 (Reader 默认平滑指向同一集群但独立连接池配额)
    void init(
        const std::string& conninfo,
        size_t min_connections = 4,
        size_t max_connections = 32
    );

    void shutdown();

    // 从指定角色连接池租借连接
    PooledConnection acquire(
        DbRole role = DbRole::Writer,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)
    );

    PooledConnection acquire_writer(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)
    ) {
        return acquire(DbRole::Writer, timeout);
    }

    PooledConnection acquire_reader(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)
    ) {
        return acquire(DbRole::Reader, timeout);
    }

    // 事务辅助执行 (强行绑定至主库 Writer 连接)
    template <typename Func>
    Result<typename std::invoke_result<Func, DbConnection&>::type::value_type> with_transaction(Func&& func) {
        auto conn = acquire_writer();
        if (!conn) {
            return std::unexpected(AppError::DatabaseError);
        }

        PgResultGuard begin_res(conn->exec("BEGIN;"));
        if (!begin_res.is_ok()) {
            return std::unexpected(AppError::DatabaseError);
        }

        auto res = func(*conn);
        if (!res) {
            PgResultGuard rollback_res(conn->exec("ROLLBACK;"));
            return std::unexpected(res.error());
        }

        PgResultGuard commit_res(conn->exec("COMMIT;"));
        if (!commit_res.is_ok()) {
            PgResultGuard rollback_res(conn->exec("ROLLBACK;"));
            return std::unexpected(AppError::DatabaseError);
        }

        return res;
    }

private:
    DbPool() = default;
    ~DbPool() { shutdown(); }

    struct SubPool {
        std::string conninfo;
        size_t max_connections{32};
        size_t total_connections{0};
        std::queue<std::unique_ptr<DbConnection>> pool;
        std::mutex mutex;
        std::condition_variable cv;
        bool is_shutdown{false};
    };

    PooledConnection acquire_from_subpool(
        SubPool& subpool,
        std::chrono::milliseconds timeout
    );

    SubPool writer_pool_;
    SubPool reader_pool_;
    bool is_initialized_{false};
};

} // namespace ev
