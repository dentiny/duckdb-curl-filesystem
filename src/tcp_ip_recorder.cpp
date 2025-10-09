#include "tcp_ip_recorder.hpp"

namespace duckdb {

/*static*/ TcpIpRecorder &TcpIpRecorder::GetInstance() {
	static auto *tcp_ip_recorder = new TcpIpRecorder {};
	return *tcp_ip_recorder;
}

void TcpIpRecorder::RecordIp(char *ip) {
	string ip_str {ip};
	std::lock_guard<std::mutex> lck(mu);
	ips.insert(std::move(ip_str));
}

unordered_set<string> TcpIpRecorder::GetAllIps() const {
	std::lock_guard<std::mutex> lck(mu);
	return ips;
}

void TcpIpRecorder::Clear() {
	std::lock_guard<std::mutex> lck(mu);
	ips.clear();
}

} // namespace duckdb
