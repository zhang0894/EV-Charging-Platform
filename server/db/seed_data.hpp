#pragma once

#include "db_pool.hpp"

namespace ev {

class SeedDataGenerator {
public:
    static bool populate_if_empty();
};

} // namespace ev
