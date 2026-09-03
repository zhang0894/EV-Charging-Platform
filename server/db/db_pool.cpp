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
    prepared_stmts_.clear();
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

bool DbConnection::prepare(const char* stmt_name, const char* query, int nParams) {
    if (!is_valid()) {
        reset();
    }
    if (!conn_) return false;
    if (prepared_stmts_.contains(stmt_name)) {
        return true;
    }
    PGresult* res = PQprepare(conn_, stmt_name, query, nParams, nullptr);
    if (!res) return false;
    bool ok = (PQresultStatus(res) == PGRES_COMMAND_OK);
    PQclear(res);
    if (ok) {
        prepared_stmts_.insert(stmt_name);
    }
    return ok;
}

PGresult* DbConnection::exec_prepared(
    const char* stmt_name,
    int nParams,
    const char* const* paramValues,
    const int* paramLengths,
    const int* paramFormats,
    int resultFormat
) {
    if (!is_valid()) {
        reset();
    }
    return PQexecPrepared(
        conn_,
        stmt_name,
        nParams,
        paramValues,
        paramLengths,
        paramFormats,
        resultFormat
    );
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
        writer_pool_.total_connections = 0;
        writer_pool_.is_shutdown = false;
        while (!writer_pool_.pool.empty()) writer_pool_.pool.pop();

        for (size_t i = 0; i < min_connections; ++i) {
            auto conn = std::make_unique<DbConnection>(writer_pool_.conninfo);
            if (conn->is_valid()) {
                writer_pool_.pool.push(std::move(conn));
                writer_pool_.total_connections++;
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
        reader_pool_.total_connections = 0;
        reader_pool_.is_shutdown = false;
        while (!reader_pool_.pool.empty()) reader_pool_.pool.pop();

        for (size_t i = 0; i < min_connections; ++i) {
            auto conn = std::make_unique<DbConnection>(reader_pool_.conninfo);
            if (conn->is_valid()) {
                reader_pool_.pool.push(std::move(conn));
                reader_pool_.total_connections++;
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
        writer_pool_.total_connections = 0;
        while (!writer_pool_.pool.empty()) writer_pool_.pool.pop();
        writer_pool_.cv.notify_all();
    }
    {
        std::lock_guard<std::mutex> lock(reader_pool_.mutex);
        reader_pool_.is_shutdown = true;
        reader_pool_.total_connections = 0;
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

    // 1. 若当前空闲池为空，但未达到最大连接数配额：立即动态扩容创建新连接，零等待！
    if (subpool.pool.empty() && subpool.total_connections < subpool.max_connections) {
        auto conn = std::make_unique<DbConnection>(subpool.conninfo);
        if (conn->is_valid()) {
            subpool.total_connections++;
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
                        if (subpool.total_connections > 0) subpool.total_connections--;
                    }
                }
            );
        }
    }

    // 2. 若当前已达最大连接数且无空闲连接：进入排队等待可用连接
    if (subpool.pool.empty()) {
        auto wait_timeout = std::min(timeout, std::chrono::milliseconds(200));
        bool acquired = subpool.cv.wait_for(lock, wait_timeout, [&subpool]() {
            return !subpool.pool.empty() || subpool.is_shutdown;
        });
        if (!acquired || subpool.is_shutdown || subpool.pool.empty()) {
            return nullptr;
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
                if (subpool.total_connections > 0) subpool.total_connections--;
            }
        }
    );
}

} // namespace ev
