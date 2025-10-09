// TcpIpRecorder is a thin wrapper around IP, which records IP address of all completed HTTP requests.

#pragma once

#include <mutex>

#include "duckdb/common/string.hpp"
#include "duckdb/common/unordered_set.hpp"

namespace duckdb {

class TcpIpRecorder {
public:
	static TcpIpRecorder &GetInstance();

	// Record IP address for a completed HTTP request.
	void RecordIp(char *ip);

	// Get all IP addresses.
	unordered_set<string> GetAllIps() const;

	// Clear all recorded IP addresses.
	void Clear();

private:
	TcpIpRecorder() = default;

	// TODO(hjiang): evict IP based on TIME_WAIT.
	mutable std::mutex mu;
	unordered_set<string> ips;
};

} // namespace duckdb
