# Pure logic: no I/O dependencies, easy to unit test.
add_library(ll_core STATIC
  core/utf8.cpp
  core/text_normalize.cpp
  core/lecture_splitter.cpp
  core/chunker.cpp
  core/cer.cpp
  core/rrf.cpp
  embed/pooling.cpp
  ocr/ctc.cpp
  ocr/layout.cpp
)
target_include_directories(ll_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(ll_pdf STATIC pdf/pdf_reader.cpp)
target_include_directories(ll_pdf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ll_pdf PUBLIC PkgConfig::POPPLER_CPP)

add_library(ll_storage STATIC
  storage/pg.cpp
  storage/document_repo.cpp
  storage/ocr_repo.cpp
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

if(LL_ENABLE_ORT)
  add_library(ll_inference STATIC inference/ort_model.cpp)
  target_include_directories(ll_inference PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(ll_inference PUBLIC onnxruntime::onnxruntime)

  add_library(ll_ocr STATIC
    ocr/text_detector.cpp
    ocr/text_recognizer.cpp
    ocr/ocr_engine.cpp
  )
  target_include_directories(ll_ocr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(ll_ocr PUBLIC ll_core ll_inference ${OpenCV_LIBS})

  add_executable(ll-model-info apps/ll_model_info.cpp)
  target_link_libraries(ll-model-info PRIVATE ll_inference)

  add_executable(ll-ocr apps/ll_ocr.cpp)
  target_link_libraries(ll-ocr PRIVATE ll_ocr ll_pdf ll_storage)

  add_library(ll_search STATIC
    embed/text_embedder.cpp
    vector/qdrant_client.cpp
    index/outbox_indexer.cpp
    search/hybrid_search.cpp
  )
  target_include_directories(ll_search PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(ll_search PUBLIC ll_core ll_inference sentencepiece::sentencepiece PkgConfig::HTTPLIB
                        PkgConfig::LIBPQXX nlohmann_json::nlohmann_json)

  add_executable(ll-indexer apps/ll_indexer.cpp)
  target_link_libraries(ll-indexer PRIVATE ll_search ll_storage)

  add_executable(ll-search apps/ll_search.cpp)
  target_link_libraries(ll-search PRIVATE ll_search ll_storage)

  add_executable(ll-api apps/ll_api.cpp)
  target_link_libraries(ll-api PRIVATE ll_search ll_storage)
endif()
