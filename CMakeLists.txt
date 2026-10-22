cmake_minimum_required(VERSION 3.5)

# Set extension name here
set(TARGET_NAME curl_httpfs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Override DuckDB encryption library, mimics httpfs extension behavior.
if(NOT EMSCRIPTEN)
  add_definitions(-DOVERRIDE_ENCRYPTION_UTILS=1)
else()
  set(DUCKDB_EXTENSION_HTTPFS_LINKED_LIBS
      "../../third_party/mbedtls/libduckdb_mbedtls.a")
endif()

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})
include_directories(src/include)
include_directories(duckdb-httpfs/src/include)
include_directories(duckdb/third_party/httplib)

set(EXTENSION_SOURCES
    duckdb-httpfs/src/create_secret_functions.cpp
    duckdb-httpfs/src/crypto.cpp
    duckdb-httpfs/src/hffs.cpp
    duckdb-httpfs/src/httpfs_extension.cpp
    duckdb-httpfs/src/http/http_request.cpp
    duckdb-httpfs/src/http/http_request_session.cpp
    duckdb-httpfs/src/http/http_settings.cpp
    duckdb-httpfs/src/http/http_state.cpp
    duckdb-httpfs/src/http/httpfs.cpp
    duckdb-httpfs/src/http/httpfs_certificates.cpp
    duckdb-httpfs/src/http/httpfs_curl_client.cpp
    duckdb-httpfs/src/http/httpfs_httplib_client.cpp
    duckdb-httpfs/src/http/httpfs_transport.cpp
    duckdb-httpfs/src/s3/s3_auth.cpp
    duckdb-httpfs/src/s3/s3_delete.cpp
    duckdb-httpfs/src/s3/s3_endpoint.cpp
    duckdb-httpfs/src/s3/s3_list.cpp
    duckdb-httpfs/src/s3/s3_provider.cpp
    duckdb-httpfs/src/s3/s3_request.cpp
    duckdb-httpfs/src/s3/s3_settings.cpp
    duckdb-httpfs/src/s3/s3_upload_session.cpp
    duckdb-httpfs/src/s3/s3_url.cpp
    duckdb-httpfs/src/s3/s3_xml_response.cpp
    duckdb-httpfs/src/s3/s3fs.cpp
    src/curl_httpfs_functions.cpp
    src/curl_httpfs_extension.cpp
    src/curl_request.cpp
    src/extension_loader_helper.cpp
    src/multi_curl_client.cpp
    src/multi_curl_manager.cpp
    src/multi_curl_util.cpp
    src/tcp_connection_fetcher.cpp
    src/tcp_connection_query_function.cpp
    src/thread_pool.cpp
    src/thread_utils.cpp)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

# Link OpenSSL in both the static library as the loadable extension
target_link_libraries(${EXTENSION_NAME} OpenSSL::SSL OpenSSL::Crypto
                      CURL::libcurl)
target_link_libraries(${LOADABLE_EXTENSION_NAME} OpenSSL::SSL OpenSSL::Crypto
                      CURL::libcurl)

if(APPLE)
  find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
  find_library(SECURITY_FRAMEWORK Security)
  target_link_libraries(${EXTENSION_NAME} ${COREFOUNDATION_FRAMEWORK}
                        ${SECURITY_FRAMEWORK})
  target_link_libraries(${LOADABLE_EXTENSION_NAME} ${COREFOUNDATION_FRAMEWORK}
                        ${SECURITY_FRAMEWORK})
endif()

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")

# Test cases.
include_directories(duckdb/third_party/catch)

add_executable(test_thread_pool unit/test_thread_pool.cpp)
target_link_libraries(test_thread_pool ${EXTENSION_NAME} duckdb_static)
link_extension_libraries(test_thread_pool "")

add_executable(multi_curl_manager_test integration/multi_curl_manager_test.cpp)
target_link_libraries(multi_curl_manager_test ${EXTENSION_NAME} duckdb_static)
link_extension_libraries(multi_curl_manager_test "")

add_executable(http_filesystem_test integration/http_filesystem_test.cpp)
target_link_libraries(http_filesystem_test ${EXTENSION_NAME} duckdb_static)
link_extension_libraries(http_filesystem_test "")

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  foreach(TARGET ${EXTENSION_NAME} ${LOADABLE_EXTENSION_NAME} test_thread_pool
                 multi_curl_manager_test http_filesystem_test)
    target_compile_options(${TARGET} PRIVATE -Wthread-safety)
  endforeach()
endif()

add_subdirectory(test/unittest)

# Benchmark
if(BUILD_BENCHMARKS)
  add_executable(multicurl_benchmark benchmark/multicurl_benchmark.cpp)
  target_link_libraries(multicurl_benchmark ${EXTENSION_NAME} duckdb_static)
  link_extension_libraries(multicurl_benchmark "")
endif()
