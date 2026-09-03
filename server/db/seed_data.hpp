#pragma once

#include "db_pool.hpp"

namespace ev {

class SeedDataGenerator {
public:
    static bool populate_if_empty();
    static bool clear_database();
};

} // namespace ev
