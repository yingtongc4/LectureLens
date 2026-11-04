cmake_minimum_required(VERSION 3.25)
project(lecturelens VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LL_ENABLE_CUDA     "Build CUDA / TensorRT inference backends" OFF)
option(LL_ENABLE_OPENVINO "Build the OpenVINO inference backend"     OFF)
option(LL_ENABLE_ORT      "Build ONNX Runtime inference backends"    ON)
option(LL_SANITIZE        "Build with AddressSanitizer + UBSan"      OFF)
option(LL_BUILD_TESTS     "Build unit tests"                         ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)
if(LL_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(POPPLER_CPP REQUIRED IMPORTED_TARGET poppler-cpp)
pkg_check_modules(LIBPQXX REQUIRED IMPORTED_TARGET libpqxx)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json 3.11 REQUIRED)

if(LL_ENABLE_ORT)
  # Prebuilt release tarball unpacked by scripts/fetch_models.sh.
  set(ORT_ROOT "$ENV{HOME}/opt/onnxruntime-linux-x64-1.31.0" CACHE PATH "ONNX Runtime install dir")
  find_library(ORT_LIB onnxruntime PATHS "${ORT_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
  add_library(onnxruntime::onnxruntime SHARED IMPORTED)
  set_target_properties(onnxruntime::onnxruntime PROPERTIES
    IMPORTED_LOCATION "${ORT_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${ORT_ROOT}/include")
  set(CMAKE_BUILD_RPATH "${ORT_ROOT}/lib")
  find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc imgcodecs)
  # Not pkg-config: Ubuntu's sentencepiece.pc requires protobuf-lite's .pc,
  # which only the unneeded libprotobuf-dev ships.
  find_library(SENTENCEPIECE_LIB sentencepiece REQUIRED)
  find_path(SENTENCEPIECE_INCLUDE sentencepiece_processor.h REQUIRED)
  add_library(sentencepiece::sentencepiece UNKNOWN IMPORTED)
  set_target_properties(sentencepiece::sentencepiece PROPERTIES
    IMPORTED_LOCATION "${SENTENCEPIECE_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${SENTENCEPIECE_INCLUDE}")
  pkg_check_modules(HTTPLIB REQUIRED IMPORTED_TARGET cpp-httplib)
endif()

add_subdirectory(src)

if(LL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
