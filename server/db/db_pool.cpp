#include "db_pool.hpp"
#include <iostream>
#include <format>

namespace ev {

DbConnection::DbConnection(const std::string& conninfo)
    : conninfo_(conninfo), conn_(PQconnectdb(conninfo.c_str())) {
}

DbConnection::~DbConnection() {
    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
    }
}

bool DbConnection::is_valid() const {
    return conn_ && PQstatus(conn_) == CONNECTION_OK;
}

void DbConnection::reset() {
    if (conn_) {
        PQreset(conn_);
    } else {
        conn_ = PQconnectdb(conninfo_.c_str());
    }
}

PGresult* DbConnection::exec_params(
    const char* command,
    int nParams,
    const Oid* paramTypes,
    const char* const* paramValues,
    const int* paramLengths,
    const int* paramFormats,
    int resultFormat
) {
    if (!is_valid()) {
        reset();
    }
    return PQexecParams(
        conn_,
        command,
        nParams,
        paramTypes,
        paramValues,
        paramLengths,
        paramFormats,
        resultFormat
    );
}

PGresult* DbConnection::exec(const char* query) {
    if (!is_valid()) {
        reset();
    }
    return PQexec(conn_, query);
}

std::string DbConnection::last_error() const {
    return conn_ ? PQerrorMessage(conn_) : "Null connection";
}

DbPool& DbPool::instance() {
    static DbPool pool;
    return pool;
}

void DbPool::init(const std::string& conninfo, size_t min_connections, size_t max_connections) {
    std::lock_guard<std::mutex> lock(mutex_);
    conninfo_ = conninfo;
    max_connections_ = max_connections;
    is_shutdown_ = false;

    // 清空历史连接
    while (!pool_.empty()) {
        pool_.pop();
    }

    // 预热建立初始连接
    for (size_t i = 0; i < min_connections; ++i) {
        auto conn = std::make_unique<DbConnection>(conninfo_);
        if (conn->is_valid()) {
            pool_.push(std::move(conn));
        } else {
            std::cerr << std::format("[DbPool Warning] Connection {} init failed: {}\n", i, conn->last_error());
        }
    }
}

void DbPool::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    is_shutdown_ = true;
    while (!pool_.empty()) {
        pool_.pop();
    }
    cv_.notify_all();
}

std::unique_ptr<DbConnection, std::function<void(DbConnection*)>> DbPool::acquire(
    std::chrono::milliseconds timeout
) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (is_shutdown_) {
        return nullptr;
    }

    if (pool_.empty()) {
        if (cv_.wait_for(lock, timeout, [this]() { return !pool_.empty() || is_shutdown_; })) {
            if (is_shutdown_ || pool_.empty()) return nullptr;
        } else {
            // 超时但未达到最大连接数时动态新建
            auto conn = std::make_unique<DbConnection>(conninfo_);
            if (!conn->is_valid()) {
                return nullptr;
            }
            auto raw_ptr = conn.release();
            return std::unique_ptr<DbConnection, std::function<void(DbConnection*)>>(
                raw_ptr,
                [this](DbConnection* ptr) {
                    if (!ptr) return;
                    std::lock_guard<std::mutex> lk(mutex_);
                    if (!is_shutdown_) {
                        pool_.push(std::unique_ptr<DbConnection>(ptr));
                        cv_.notify_one();
                    } else {
                        delete ptr;
                    }
                }
            );
        }
    }

    auto conn = std::move(pool_.front());
    pool_.pop();

    if (!conn->is_valid()) {
        conn->reset();
    }

    auto raw_ptr = conn.release();
    return std::unique_ptr<DbConnection, std::function<void(DbConnection*)>>(
        raw_ptr,
        [this](DbConnection* ptr) {
            if (!ptr) return;
            std::lock_guard<std::mutex> lk(mutex_);
            if (!is_shutdown_) {
                pool_.push(std::unique_ptr<DbConnection>(ptr));
                cv_.notify_one();
            } else {
                delete ptr;
            }
        }
    );
}

} // namespace ev
