#include "db_pool.hpp"
#include <iostream>
#include <print>
#include <format>

namespace ev {

DbConnection::DbConnection(const std::string& conninfo)
    : conninfo_(conninfo) {
    conn_ = PQconnectdb(conninfo_.c_str());
}

DbConnection::~DbConnection() {
    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
    }
}

bool DbConnection::is_valid() const {
    return conn_ && (PQstatus(conn_) == CONNECTION_OK);
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

void DbPool::init(
    const std::string& writer_conninfo,
    const std::string& reader_conninfo,
    size_t min_connections,
    size_t max_connections
) {
    shutdown();

    // 1. 初始化主库连接池 (Writer)
    {
        std::lock_guard<std::mutex> lock(writer_pool_.mutex);
        writer_pool_.conninfo = writer_conninfo;
        writer_pool_.max_connections = max_connections;
        writer_pool_.is_shutdown = false;
        while (!writer_pool_.pool.empty()) writer_pool_.pool.pop();

        for (size_t i = 0; i < min_connections; ++i) {
            auto conn = std::make_unique<DbConnection>(writer_pool_.conninfo);
            if (conn->is_valid()) {
                writer_pool_.pool.push(std::move(conn));
            } else {
                std::cerr << std::format("[DbPool Warning] Writer conn {} init failed: {}\n", i, conn->last_error());
            }
        }
    }

    // 2. 初始化从库/只读副本连接池 (Reader)
    {
        std::lock_guard<std::mutex> lock(reader_pool_.mutex);
        reader_pool_.conninfo = reader_conninfo.empty() ? writer_conninfo : reader_conninfo;
        reader_pool_.max_connections = max_connections;
        reader_pool_.is_shutdown = false;
        while (!reader_pool_.pool.empty()) reader_pool_.pool.pop();

        for (size_t i = 0; i < min_connections; ++i) {
            auto conn = std::make_unique<DbConnection>(reader_pool_.conninfo);
            if (conn->is_valid()) {
                reader_pool_.pool.push(std::move(conn));
            } else {
                std::cerr << std::format("[DbPool Warning] Reader conn {} init failed: {}\n", i, conn->last_error());
            }
        }
    }

    is_initialized_ = true;
    std::println("  [DbPool] 读写分离架构初始化完成 (主库写池配额: {}, 从库只读副本池配额: {})", max_connections, max_connections);
}

void DbPool::init(const std::string& conninfo, size_t min_connections, size_t max_connections) {
    init(conninfo, conninfo, min_connections, max_connections);
}

void DbPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(writer_pool_.mutex);
        writer_pool_.is_shutdown = true;
        while (!writer_pool_.pool.empty()) writer_pool_.pool.pop();
        writer_pool_.cv.notify_all();
    }
    {
        std::lock_guard<std::mutex> lock(reader_pool_.mutex);
        reader_pool_.is_shutdown = true;
        while (!reader_pool_.pool.empty()) reader_pool_.pool.pop();
        reader_pool_.cv.notify_all();
    }
    is_initialized_ = false;
}

DbPool::PooledConnection DbPool::acquire(DbRole role, std::chrono::milliseconds timeout) {
    if (role == DbRole::Reader) {
        return acquire_from_subpool(reader_pool_, timeout);
    }
    return acquire_from_subpool(writer_pool_, timeout);
}

DbPool::PooledConnection DbPool::acquire_from_subpool(
    SubPool& subpool,
    std::chrono::milliseconds timeout
) {
    std::unique_lock<std::mutex> lock(subpool.mutex);

    if (subpool.is_shutdown) {
        return nullptr;
    }

    if (subpool.pool.empty()) {
        if (subpool.cv.wait_for(lock, timeout, [&subpool]() { return !subpool.pool.empty() || subpool.is_shutdown; })) {
            if (subpool.is_shutdown || subpool.pool.empty()) return nullptr;
        } else {
            // 超时但未达到最大连接数时新建
            auto conn = std::make_unique<DbConnection>(subpool.conninfo);
            if (!conn->is_valid()) {
                return nullptr;
            }
            auto raw_ptr = conn.release();
            return PooledConnection(
                raw_ptr,
                [&subpool](DbConnection* ptr) {
                    if (!ptr) return;
                    std::lock_guard<std::mutex> lk(subpool.mutex);
                    if (!subpool.is_shutdown) {
                        subpool.pool.push(std::unique_ptr<DbConnection>(ptr));
                        subpool.cv.notify_one();
                    } else {
                        delete ptr;
                    }
                }
            );
        }
    }

    auto conn = std::move(subpool.pool.front());
    subpool.pool.pop();

    if (!conn->is_valid()) {
        conn->reset();
    }

    auto raw_ptr = conn.release();
    return PooledConnection(
        raw_ptr,
        [&subpool](DbConnection* ptr) {
            if (!ptr) return;
            std::lock_guard<std::mutex> lk(subpool.mutex);
            if (!subpool.is_shutdown) {
                subpool.pool.push(std::unique_ptr<DbConnection>(ptr));
                subpool.cv.notify_one();
            } else {
                delete ptr;
            }
        }
    );
}

} // namespace ev
