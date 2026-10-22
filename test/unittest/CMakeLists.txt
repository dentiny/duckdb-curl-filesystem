include_directories(${CMAKE_SOURCE_DIR}/src/include)
include_directories(${CMAKE_SOURCE_DIR}/duckdb-httpfs/src/include)
include_directories(${CMAKE_SOURCE_DIR}/duckdb/third_party/catch)

set(CURL_HTTPFS_UNITTEST_OBJECTS main.cpp test_multi_curl_error.cpp)

add_executable(unittest_curl_httpfs ${CURL_HTTPFS_UNITTEST_OBJECTS})

target_link_libraries(unittest_curl_httpfs ${EXTENSION_NAME} duckdb_static)

link_extension_libraries(unittest_curl_httpfs "")

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(unittest_curl_httpfs PRIVATE -Wthread-safety)
endif()
