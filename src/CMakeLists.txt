# Pure logic: no I/O dependencies, easy to unit test.
add_library(ll_core STATIC
  core/utf8.cpp
  core/text_normalize.cpp
  core/lecture_splitter.cpp
  core/chunker.cpp
)
target_include_directories(ll_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(ll_pdf STATIC pdf/pdf_reader.cpp)
target_include_directories(ll_pdf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ll_pdf PUBLIC PkgConfig::POPPLER_CPP)

add_library(ll_storage STATIC
  storage/pg.cpp
  storage/document_repo.cpp
)
target_include_directories(ll_storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ll_storage PUBLIC ll_core PkgConfig::LIBPQXX nlohmann_json::nlohmann_json)

add_library(ll_ingest STATIC
  ingest/pdf_ingestor.cpp
  common/sha256.cpp
)
target_include_directories(ll_ingest PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ll_ingest PUBLIC ll_core ll_pdf ll_storage OpenSSL::Crypto)

add_executable(ll-ingest apps/ll_ingest.cpp)
target_link_libraries(ll-ingest PRIVATE ll_ingest)

