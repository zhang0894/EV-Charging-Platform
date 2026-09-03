#pragma once

#include "db_pool.hpp"
#include "../common/models.hpp"
#include "../common/types.hpp"

#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <chrono>
#include <format>
#include <iostream>
#include <algorithm>
#include <optional>

namespace ev {

struct WalletFlowItem {
    std::string id;
    int64_t user_id{0};
    int flow_type{1}; // 1: 充值, 2: 充电扣费, 3: 充电退补, 4: 管理员调账
    int64_t amount_cents{0};
    int64_t balance_before_cents{0};
    int64_t balance_after_cents{0};
    std::string related_order_id;
    int64_t operator_id{0};
    std::string remark;
    std::string idempotent_key;
    int64_t created_at{0};
};

class AsyncFlowPersister {
public:
    static AsyncFlowPersister& instance() {
        static AsyncFlowPersister inst;
        return inst;
    }

    // 投递流水记录至异步批量持久化队列
    void enqueue(const WalletFlowItem& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        queue_.push_back(item);

        // 缓存幂等性，提供微秒级快速防重查验
        if (!item.idempotent_key.empty()) {
            if (idempotent_cache_.size() > 20000) {
                idempotent_cache_.clear();
            }
            idempotent_cache_[item.idempotent_key] = TransactionItemDTO{
                .transaction_id = item.id,
                .flow_type = item.flow_type,
                .flow_type_desc = std::string(to_string(static_cast<FlowType>(item.flow_type))),
                .amount = cents_to_yuan(item.amount_cents),
                .amount_cents = item.amount_cents,
                .balance_before = cents_to_yuan(item.balance_before_cents),
                .balance_after = cents_to_yuan(item.balance_after_cents),
                .related_order_id = item.related_order_id,
                .remark = item.remark,
                .created_at = item.created_at
            };
        }

        // 维护未落盘流水缓冲区，保障写后即读一致性
        pending_by_user_[item.user_id].push_back(item);

        if (queue_.size() >= BATCH_THRESHOLD) {
            cv_.notify_one();
        }
    }

    // 查询内存中的幂等缓存 (O(1))
    std::optional<TransactionItemDTO> get_cached_idempotent(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = idempotent_cache_.find(key);
        if (it != idempotent_cache_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    // 获取某用户当前未落盘的内存待刷流水 (由新到旧排序)
    std::vector<TransactionItemDTO> get_pending_for_user(int64_t user_id, int flow_type_filter = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<TransactionItemDTO> results;
        auto it = pending_by_user_.find(user_id);
        if (it == pending_by_user_.end()) {
            return results;
        }

        for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) {
            if (flow_type_filter == 0 || rit->flow_type == flow_type_filter) {
                results.push_back(TransactionItemDTO{
                    .transaction_id = rit->id,
                    .flow_type = rit->flow_type,
                    .flow_type_desc = std::string(to_string(static_cast<FlowType>(rit->flow_type))),
                    .amount = cents_to_yuan(rit->amount_cents),
                    .amount_cents = rit->amount_cents,
                    .balance_before = cents_to_yuan(rit->balance_before_cents),
                    .balance_after = cents_to_yuan(rit->balance_after_cents),
                    .related_order_id = rit->related_order_id,
                    .remark = rit->remark,
                    .created_at = rit->created_at
                });
            }
        }
        return results;
    }

    // 同步强制刷盘（等待队列全部落盘完成）
    void flush() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.notify_one();
        flush_cv_.wait(lock, [this] {
            return queue_.empty();
        });
    }

    // 安全关闭持久化后台线程
    void shutdown() {
        if (!running_.exchange(false)) {
            return;
        }
        cv_.notify_all();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        // 确保最终队列彻底写入
        persist_remaining();
    }

private:
    AsyncFlowPersister() : running_(true) {
        worker_thread_ = std::thread(&AsyncFlowPersister::worker_loop, this);
    }

    ~AsyncFlowPersister() {
        shutdown();
    }

    AsyncFlowPersister(const AsyncFlowPersister&) = delete;
    AsyncFlowPersister& operator=(const AsyncFlowPersister&) = delete;

    static std::string escape_sql(std::string_view s) {
        std::string res;
        res.reserve(s.size() + 4);
        for (char c : s) {
            if (c == '\'') res += "''";
            else res += c;
        }
        return res;
    }

    void worker_loop() {
        while (running_) {
            std::vector<WalletFlowItem> batch;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait_for(lock, std::chrono::milliseconds(20), [this] {
                    return !running_ || queue_.size() >= BATCH_THRESHOLD;
                });

                if (queue_.empty()) {
                    continue;
                }

                batch = std::move(queue_);
                queue_.clear();
            }

            if (!batch.empty()) {
                persist_batch(batch);

                std::unique_lock<std::mutex> lock(mutex_);
                for (const auto& item : batch) {
                    auto it = pending_by_user_.find(item.user_id);
                    if (it != pending_by_user_.end()) {
                        auto& dq = it->second;
                        dq.erase(std::remove_if(dq.begin(), dq.end(), [&](const WalletFlowItem& x) {
                            return x.id == item.id;
                        }), dq.end());
                        if (dq.empty()) {
                            pending_by_user_.erase(it);
                        }
                    }
                }
                flush_cv_.notify_all();
            }
        }
    }

    void persist_remaining() {
        std::vector<WalletFlowItem> batch;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            batch = std::move(queue_);
            queue_.clear();
        }
        if (!batch.empty()) {
            persist_batch(batch);
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_by_user_.clear();
        }
    }

    void persist_batch(const std::vector<WalletFlowItem>& batch) {
        if (batch.empty()) return;

        auto conn = DbPool::instance().acquire();
        if (!conn) {
            std::cerr << "[AsyncFlowPersister Error] Cannot acquire connection from DbPool to persist batch (" << batch.size() << " items)\n";
            return;
        }

        std::string sql = "INSERT INTO wallet_transaction_flows (id, user_id, flow_type, amount_cents, balance_before_cents, balance_after_cents, related_order_id, operator_id, remark, idempotent_key, created_at) VALUES ";
        for (size_t i = 0; i < batch.size(); ++i) {
            const auto& item = batch[i];
            if (i > 0) sql += ", ";
            sql += std::format(
                "('{}', {}, {}, {}, {}, {}, '{}', {}, '{}', '{}', {})",
                item.id,
                item.user_id,
                item.flow_type,
                item.amount_cents,
                item.balance_before_cents,
                item.balance_after_cents,
                escape_sql(item.related_order_id),
                item.operator_id,
                escape_sql(item.remark),
                escape_sql(item.idempotent_key),
                item.created_at
            );
        }
        sql += " ON CONFLICT (id) DO NOTHING;";

        PgResultGuard res(conn->exec(sql.c_str()));
        if (!res.is_ok()) {
            std::cerr << "[AsyncFlowPersister Error] Batch insert failed: " << conn->last_error() << "\n";
        }
    }

    static constexpr size_t BATCH_THRESHOLD = 50;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable flush_cv_;
    std::atomic<bool> running_{true};
    std::thread worker_thread_;

    std::vector<WalletFlowItem> queue_;
    std::unordered_map<std::string, TransactionItemDTO> idempotent_cache_;
    std::unordered_map<int64_t, std::deque<WalletFlowItem>> pending_by_user_;
};

} // namespace ev
