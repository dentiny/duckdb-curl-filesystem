#pragma once

#include <curl/curl.h>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

#include "duckdb/common/unique_ptr.hpp"

using namespace duckdb;

// ----------------------
// Forward declarations
// ----------------------

struct HTTPResponse;
struct RequestInfo;

// Basic HTTP response object
struct HTTPResponse {
    int status_code;
    std::string body;
    explicit HTTPResponse(int code) : status_code(code) {}
};

// Request info
struct RequestInfo {
    std::string url;
    std::string body;
    uint16_t response_code = 0;
};

// One HTTP request bound to an easy handle
struct CurlRequest {
    unique_ptr<RequestInfo> info;
    std::promise<std::unique_ptr<HTTPResponse>> done;
    std::atomic<bool> completed{false};
    CURL *easy = nullptr;

    explicit CurlRequest(std::string url);
    ~CurlRequest();

    static size_t WriteBody(void *contents, size_t size, size_t nmemb, void *userp);
};

// Global curl state (multi, epoll, timer)
struct GlobalInfo {
    int epoll_fd = -1;
    int timer_fd = -1;
    CURLM *multi = nullptr;
    std::mutex mu;          // protect multi handle
    int still_running = 0;
};

// Manager
class MultiCurlManager {
public:
    static MultiCurlManager &GetInstance();
    ~MultiCurlManager() = default;

    MultiCurlManager(const MultiCurlManager &) = delete;
    MultiCurlManager &operator=(const MultiCurlManager &) = delete;

    // Handle HTTP request, block until completion.
    std::unique_ptr<HTTPResponse> HandleRequest(std::unique_ptr<CurlRequest> request);

private:
    MultiCurlManager();

    void HandleEvent();
    void ProcessPendingRequests();

    std::unique_ptr<GlobalInfo> global_info;
    std::queue<std::unique_ptr<CurlRequest>> pending_requests_;
    std::unordered_map<CURL*, std::unique_ptr<CurlRequest>> ongoing_requests_;
    std::thread bkg_thread;
};
