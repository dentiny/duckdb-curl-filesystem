cmake_minimum_required(VERSION 3.5)

# Set extension name here
set(TARGET_NAME curl_httpfs)

set(CMAKE_CXX_STANDARD 14)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-c++17-extensions")

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})
include_directories(src/include)
include_directories(duckdb/third_party/httplib)

set(EXTENSION_SOURCES
    src/create_secret_functions.cpp
    src/crypto.cpp
    src/curl_httpfs_extension.cpp
    src/curl_request.cpp
    src/hash_functions.cpp
    src/hffs.cpp
    src/http_state.cpp
    src/httpfs.cpp
    src/httpfs_curl_client.cpp
    src/httpfs_httplib_client.cpp
    src/multi_curl_manager.cpp
    src/s3fs.cpp
    src/tcp_connection_fetcher.cpp
    src/tcp_connection_query_function.cpp
    src/tcp_ip_recorder.cpp
    src/thread_pool.cpp
    src/thread_utils.cpp)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

# Link OpenSSL in both the static library as the loadable extension
target_link_libraries(${EXTENSION_NAME} OpenSSL::SSL OpenSSL::Crypto
                      CURL::libcurl)
target_link_libraries(${LOADABLE_EXTENSION_NAME} OpenSSL::SSL OpenSSL::Crypto
                      CURL::libcurl)

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")

# Test cases.
include_directories(duckdb/third_party/catch)

add_executable(test_thread_pool unit/test_thread_pool.cpp)
target_link_libraries(test_thread_pool ${EXTENSION_NAME})

add_executable(multi_curl_manager_test integration/multi_curl_manager_test.cpp)
target_link_libraries(multi_curl_manager_test ${EXTENSION_NAME})

add_executable(http_filesystem_test integration/http_filesystem_test.cpp)
target_link_libraries(http_filesystem_test ${EXTENSION_NAME})

# Benchmark
add_executable(multicurl_benchmark benchmark/multicurl_benchmark.cpp)
target_link_libraries(multicurl_benchmark ${EXTENSION_NAME})
