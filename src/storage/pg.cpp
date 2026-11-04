#include "storage/pg.hpp"

#include <cstdlib>

namespace ll {

std::string pg_dsn_from_env() {
    if (const char* dsn = std::getenv("LL_PG_DSN"); dsn && *dsn) return dsn;
    return "postgresql://lecturelens:lecturelens@localhost:5432/lecturelens";
}

}  // namespace ll
