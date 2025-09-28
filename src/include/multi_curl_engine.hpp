// MultiCurlManager is a class which wraps around events managed by multi-curl, and use epoll as polling engine.

#pragma once

#include <curl/curl.h>

#include <thread>

namespace duckdb {

class MultiCurlManager {
public:
	MultiCurlManager();
	~MultiCurlManager();
	MultiCurlManager(const MultiCurlManager &) = delete;
	MultiCurlManager &operator=(const MultiCurlManager &) = delete;
	MultiCurlManager(MultiCurlManager &&) = default;
	MultiCurlManager &operator=(MultiCurlManager &&) = default;

private:
	// Poll and handle epoll events.
	void HandleEvent();

	int epoll_fd = -1;
	int timer_fd = -1;
	CURLM *multicurl = nullptr;

	// Background thread to handle all events.
	std::thread bkg_thread;
};

} // namespace duckdb
