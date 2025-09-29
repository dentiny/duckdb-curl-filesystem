cmake_minimum_required(VERSION 3.5)

# Set extension name here
set(TARGET_NAME quack)

# DuckDB's extension distribution supports vcpkg. As such, dependencies can be added in ./vcpkg.json and then
# used in cmake with find_package. Feel free to remove or replace with other dependencies.
# Note that it should also be removed from vcpkg.json to prevent needlessly installing it..
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
    src/hash_functions.cpp
    src/hffs.cpp
    src/http_state.cpp
    src/httpfs.cpp
    src/httpfs_curl_client.cpp
    src/httpfs_extension.cpp
    src/httpfs_httplib_client.cpp
    src/multi_curl_manager.cpp
    src/quack_extension.cpp
    src/s3fs.cpp)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

# Link OpenSSL in both the static library as the loadable extension
target_link_libraries(${EXTENSION_NAME} OpenSSL::SSL OpenSSL::Crypto CURL::libcurl)
target_link_libraries(${LOADABLE_EXTENSION_NAME} OpenSSL::SSL OpenSSL::Crypto CURL::libcurl)

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
