#include "multi_curl_manager.hpp"

#include <array>
#include <cstring>
#include <iostream>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "duckdb/common/unique_ptr.hpp"
#include "duckdb/common/helper.hpp"

// ----------------------
// Internal SockInfo
// ----------------------

namespace {
struct SockInfo {
    curl_socket_t sockfd = 0;
    CURL *easy = nullptr;
    int action = 0;
    GlobalInfo *global = nullptr;
};
} // namespace

// ----------------------
// Callbacks
// ----------------------

static void CheckMulti(GlobalInfo *g) {
    CURLMsg *msg = nullptr;
    int msgs_left = 0;

    while ((msg = curl_multi_info_read(g->multi, &msgs_left))) {
        if (msg->msg == CURLMSG_DONE) {
            CURL *easy = msg->easy_handle;
            CurlRequest *req = nullptr;
            curl_easy_getinfo(easy, CURLINFO_PRIVATE, &req);

            long response_code = 0;
            curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &response_code);
            req->info->response_code = static_cast<uint16_t>(response_code);

			HTTPStatusCode status_code = HTTPUtil::ToStatusCode(req->info->response_code);
            auto resp = make_uniq<HTTPResponse>(status_code);
            resp->body = req->info->body;
			resp->url = req->info->url;

            req->done.set_value(std::move(resp));
            req->completed.store(true);

            curl_multi_remove_handle(g->multi, easy);
        }
    }
}

static void TimerCallback(GlobalInfo *g, int revents) {
    uint64_t count = 0;
    (void)revents;
    (void)read(g->timer_fd, &count, sizeof(uint64_t));

    curl_multi_socket_action(g->multi, CURL_SOCKET_TIMEOUT, 0, &g->still_running);
    CheckMulti(g);
}

static void EventCallback(GlobalInfo *g, int fd, int revents) {
    int action = ((revents & EPOLLIN) ? CURL_CSELECT_IN : 0) |
                 ((revents & EPOLLOUT) ? CURL_CSELECT_OUT : 0);

    curl_multi_socket_action(g->multi, fd, action, &g->still_running);
    CheckMulti(g);

    if (g->still_running <= 0) {
        struct itimerspec its;
        memset(&its, 0, sizeof(its));
        timerfd_settime(g->timer_fd, 0, &its, NULL);
    }
}

static void RemoveSockInfo(SockInfo *f, GlobalInfo *g) {
    if (f) {
        if (f->sockfd) {
            epoll_ctl(g->epoll_fd, EPOLL_CTL_DEL, f->sockfd, NULL);
        }
        delete f;
    }
}

static void SetSockInfo(SockInfo *f, curl_socket_t s, CURL *e, int act, GlobalInfo *g) {
    struct epoll_event ev;
    int kind = ((act & CURL_POLL_IN) ? EPOLLIN : 0) |
               ((act & CURL_POLL_OUT) ? EPOLLOUT : 0);

    if (f->sockfd) {
        epoll_ctl(g->epoll_fd, EPOLL_CTL_DEL, f->sockfd, NULL);
    }

    f->sockfd = s;
    f->action = act;
    f->easy = e;

    ev.events = kind;
    ev.data.fd = s;
    epoll_ctl(g->epoll_fd, EPOLL_CTL_ADD, s, &ev);
}

static void AddSockInfo(curl_socket_t s, CURL *easy, int action, GlobalInfo *g) {
    auto *fdp = new SockInfo();
    fdp->global = g;
    SetSockInfo(fdp, s, easy, action, g);
    curl_multi_assign(g->multi, s, fdp);
}

static int SocketCallback(CURL *e, curl_socket_t s, int what, void *cbp, void *sockp) {
    auto *g = static_cast<GlobalInfo *>(cbp);
    auto *fdp = static_cast<SockInfo *>(sockp);

    if (what == CURL_POLL_REMOVE) {
        RemoveSockInfo(fdp, g);
    } else {
        if (!fdp) {
            AddSockInfo(s, e, what, g);
        } else {
            SetSockInfo(fdp, s, e, what, g);
        }
    }
    return 0;
}

static int MultiTimerCallback(CURLM *multi, long timeout_ms, GlobalInfo *g) {
    struct itimerspec its;
    if (timeout_ms > 0) {
        its.it_interval.tv_sec = 0;
        its.it_interval.tv_nsec = 0;
        its.it_value.tv_sec = timeout_ms / 1000;
        its.it_value.tv_nsec = (timeout_ms % 1000) * 1000 * 1000;
    } else if (timeout_ms == 0) {
        its.it_interval.tv_sec = 0;
        its.it_interval.tv_nsec = 0;
        its.it_value.tv_sec = 0;
        its.it_value.tv_nsec = 1;
    } else {
        memset(&its, 0, sizeof(its));
    }
    timerfd_settime(g->timer_fd, 0, &its, NULL);
    return 0;
}

// ----------------------
// MultiCurlManager impl
// ----------------------

/*static*/ MultiCurlManager &MultiCurlManager::GetInstance() {
    static auto *manager = new MultiCurlManager();
    return *manager;
}

MultiCurlManager::MultiCurlManager() : global_info(make_uniq<GlobalInfo>()) {
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd == -1) throw std::runtime_error("epoll_create1 failed");

    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (tfd == -1) throw std::runtime_error("timerfd_create failed");

    struct itimerspec its;
    memset(&its, 0, sizeof(its));
    its.it_value.tv_sec = 1;
    timerfd_settime(tfd, 0, &its, NULL);

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = tfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, tfd, &ev);

    global_info->epoll_fd = epfd;
    global_info->timer_fd = tfd;
    global_info->multi = curl_multi_init();

    curl_multi_setopt(global_info->multi, CURLMOPT_SOCKETFUNCTION, SocketCallback);
    curl_multi_setopt(global_info->multi, CURLMOPT_SOCKETDATA, global_info.get());
    curl_multi_setopt(global_info->multi, CURLMOPT_TIMERFUNCTION, MultiTimerCallback);
    curl_multi_setopt(global_info->multi, CURLMOPT_TIMERDATA, global_info.get());

    bkg_thread = std::thread([this]() { HandleEvent(); });
}

void MultiCurlManager::HandleEvent() {
    std::array<epoll_event, 32> events{};
    while (true) {
        int nfds = epoll_wait(global_info->epoll_fd, events.data(), events.size(), 1000);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }
        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == global_info->timer_fd) {
                TimerCallback(global_info.get(), events[i].events);
            } else {
                EventCallback(global_info.get(), events[i].data.fd, events[i].events);
            }
        }
        ProcessPendingRequests();
    }
}

void MultiCurlManager::ProcessPendingRequests() {
    while (!pending_requests_.empty()) {
        auto req = std::move(pending_requests_.front());
        pending_requests_.pop();

        CURL *easy = req->easy;
        ongoing_requests_[easy] = std::move(req);

        curl_easy_setopt(easy, CURLOPT_PRIVATE, ongoing_requests_[easy].get());
        curl_multi_add_handle(global_info->multi, easy);
    }
}

unique_ptr<HTTPResponse> MultiCurlManager::HandleRequest(unique_ptr<CurlRequest> request) {
    auto fut = request->done.get_future();
    {
        std::lock_guard<std::mutex> lck(global_info->mu);
        pending_requests_.emplace(std::move(request));
    }
    return fut.get();
}
