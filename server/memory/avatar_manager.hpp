#pragma once

#include "../common/models.hpp"
#include "../common/error.hpp"
#include "../common/types.hpp"
#include "../db/db_repository.hpp"
#include <string>
#include <string_view>
#include <optional>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <format>
#include <vector>

namespace ev {

struct CachedAvatar {
    std::string content_type{"image/png"};
    std::string data;
    std::string etag;
    int64_t updated_at{0};
};

class AvatarManager {
public:
    static AvatarManager& instance() {
        static AvatarManager inst;
        return inst;
    }

    // 获取用户真实头像 (优先微秒级内存缓存，未命中时回源数据库)
    std::optional<CachedAvatar> get_avatar(int64_t user_id) {
        {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            auto it = cache_.find(user_id);
            if (it != cache_.end()) {
                return it->second;
            }
        }

        // 回源数据库查询
        auto db_res = DbRepository::instance().get_user_avatar(user_id);
        if (!db_res || !(*db_res)) {
            return std::nullopt;
        }

        const auto& m = **db_res;
        CachedAvatar cached{
            .content_type = m.content_type,
            .data = m.avatar_data,
            .etag = std::format("\"av_{}_{}\"", user_id, m.updated_at),
            .updated_at = m.updated_at
        };

        // 写入内存多级缓存
        {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            // 内存容量保护：若超出最大条目，剔除最老条目
            if (cache_.size() >= MAX_CACHE_ENTRIES) {
                cache_.erase(cache_.begin());
            }
            cache_[user_id] = cached;
        }

        return cached;
    }

    // 保存并持久化用户真实头像 (写入数据库 + 刷新内存缓存)
    Result<UploadAvatarResponseData> save_avatar(
        int64_t user_id,
        std::string_view content_type,
        std::string_view binary_data
    ) {
        constexpr size_t MAX_AVATAR_SIZE = 1024 * 1024; // 1MB 严格限制 (< 1MB)
        if (binary_data.size() >= MAX_AVATAR_SIZE) {
            return std::unexpected(AppError::PayloadTooLarge);
        }
        if (binary_data.empty()) {
            return std::unexpected(AppError::InvalidParameters);
        }

        std::string ct(content_type);
        if (ct.empty()) {
            ct = detect_image_content_type(binary_data);
        }

        int64_t now = current_time_ms();

        // 1. 持久化存入 PostgreSQL user_avatars 表
        auto db_res = DbRepository::instance().save_user_avatar(user_id, ct, binary_data);
        if (!db_res) {
            return std::unexpected(db_res.error());
        }

        // 2. 写入内存高速缓存
        CachedAvatar cached{
            .content_type = ct,
            .data = std::string(binary_data),
            .etag = std::format("\"av_{}_{}\"", user_id, now),
            .updated_at = now
        };

        {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            if (cache_.size() >= MAX_CACHE_ENTRIES) {
                cache_.erase(cache_.begin());
            }
            cache_[user_id] = std::move(cached);
        }

        return UploadAvatarResponseData{
            .user_id = user_id,
            .content_type = ct,
            .file_size = static_cast<int>(binary_data.size()),
            .updated_at = now
        };
    }

    // 清空缓存与存储
    void clear() {
        {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            cache_.clear();
        }
        DbRepository::instance().clear_user_avatars();
    }

    void invalidate(int64_t user_id) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        cache_.erase(user_id);
    }

    // 智能魔数推导图像 MIME 类型
    static std::string detect_image_content_type(std::string_view data) noexcept {
        if (data.size() >= 8) {
            const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data.data());
            // PNG: 89 50 4E 47 0D 0A 1A 0A
            if (bytes[0] == 0x89 && bytes[1] == 0x50 && bytes[2] == 0x4E && bytes[3] == 0x47) {
                return "image/png";
            }
            // JPEG: FF D8 FF
            if (bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) {
                return "image/jpeg";
            }
            // GIF: GIF87a / GIF89a
            if (bytes[0] == 'G' && bytes[1] == 'I' && bytes[2] == 'F') {
                return "image/gif";
            }
            // WEBP: RIFF....WEBP
            if (bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F' &&
                data.size() >= 12 && data.substr(8, 4) == "WEBP") {
                return "image/webp";
            }
        }
        return "image/png";
    }

private:
    AvatarManager() = default;

    static constexpr size_t MAX_CACHE_ENTRIES = 200; // 最多内存保留 200 个高频头像 (~50MB)
    std::unordered_map<int64_t, CachedAvatar> cache_;
    mutable std::shared_mutex mutex_;
};

} // namespace ev
