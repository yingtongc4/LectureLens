#pragma once

#include <string>

namespace ll {

// Connection string from LL_PG_DSN, defaulting to the docker-compose database.
std::string pg_dsn_from_env();

}  // namespace ll
