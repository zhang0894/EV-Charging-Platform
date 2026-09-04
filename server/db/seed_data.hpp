#pragma once

#include "db_pool.hpp"
#include <string>

namespace ev {

class SeedDataGenerator {
public:
    // 清空数据库所有业务表数据并重置自增主键序列
    static bool clear_database();

    // 从 data_dir 下的本地 JSON 文件通过 Glaze 解析并批量事务导入 PostgreSQL
    static bool import_from_json(const std::string& data_dir = "data");

    // 检查数据库是否为空，若为空则自动装载
    static bool populate_if_empty(const std::string& data_dir = "data");
};

} // namespace ev
