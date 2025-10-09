#include "tcp_connection_fetcher.hpp"

#include "duckdb/common/string.hpp"
#include "duckdb/common/unordered_map.hpp"

//===--------------------------------------------------------------------===//
// MacOs implementation
//===--------------------------------------------------------------------===//
#if defined(__APPLE__) || defined(__MACH__)

#include <cstdio>
#include <sstream>

namespace duckdb {

std::unordered_map<std::string, int> GetTcpConnectionNum() {
	std::unordered_map<std::string, int> aggregated_tcp_conns;

	FILE *fp = popen("netstat -anv -p tcp", "r");
	if (fp == nullptr) {
		throw std::runtime_error("failed to run netstat");
	}

	char line[512];

	// Skip the header
	while (fgets(line, sizeof(line), fp)) {
		std::string s(line);
		if (s.find("Proto") != std::string::npos) {
			break;
		}
	}

	while (fgets(line, sizeof(line), fp)) {
		std::string s(line);
		std::istringstream iss(s);

		std::string proto, recvq, sendq, local, remote, state;
		iss >> proto >> recvq >> sendq >> local >> remote >> state;

		if (proto != "tcp" && proto != "tcp4" && proto != "tcp6") {
			continue;
		}
		if (remote.empty() || state.empty()) {
			continue;
		}

		// Extract IP (before ':').
		auto colon = remote.find('.');
		if (colon == std::string::npos)
			continue;

		// IPv4 style “remote.port”.
		// We can find the *last* '.' as separator.
		size_t last_dot = remote.rfind('.');
		std::string ip = (last_dot == std::string::npos) ? remote : remote.substr(0, last_dot);
		++aggregated_tcp_conns[std::move(ip)];
	}

	pclose(fp);
	return aggregated_tcp_conns;
}
} // namespace duckdb

//===--------------------------------------------------------------------===//
// Linux implementation
//===--------------------------------------------------------------------===//
#else

#include <arpa/inet.h>
#include <array>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>
#include <sstream>

#include "syscall_macros.hpp"
#include "tcp_ip_recorder.hpp"

namespace duckdb {

namespace {
constexpr const char *IPV4_TCP_PROC_FS_PATH = "/proc/net/tcp";
constexpr const char *IPV6_TCP_PROC_FS_PATH = "/proc/net/tcp6";

string HexToIP(const std::string &hex) {
	if (hex.size() != 8) {
		return "0.0.0.0";
	}

	// Read little-endian 32-bit into reverse byte order.
	std::array<unsigned char, 4> bytes {};
	for (int idx = 0; idx < 4; ++idx) {
		string byte_str = hex.substr(idx * 2, 2);
		bytes[idx] = static_cast<unsigned char>(std::stoul(byte_str, nullptr, 16));
	}

	// Reverse the order of 4 bytes.
	std::array<unsigned char, 4> reversed = {bytes[3], bytes[2], bytes[1], bytes[0]};

	// Convert IPv4 and IPv6 addresses from binary to text form.
	std::array<char, INET_ADDRSTRLEN> buf {};
	inet_ntop(AF_INET, reversed.data(), buf.data(), sizeof(buf));
	return string(buf.data());
}

// @param path: local procfs path.
// @param[out] per_remote_ip: aggregated <IP, count> pair.
void ParseProcTCP(const char *path, std::unordered_map<std::string, int> &per_remote_ip) {
	int fd = open(path, O_RDONLY);
	SYSCALL_THROW_IF_ERROR(fd);

	constexpr size_t BUF_SIZE = 65536;
	string content;
	content.reserve(BUF_SIZE);
	char buf[BUF_SIZE];
	ssize_t n = 0;
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		content.append(buf, n);
	}
	const int ret = close(fd);
	SYSCALL_THROW_IF_ERROR(ret);

	// Ignore the first line, which is the prompt line.
	// For example, "State Recv-Q Send-Q Local Address:Port   Peer Address:Port Process".
	std::istringstream iss(content);
	string cur_line;
	std::getline(iss, cur_line);

	while (std::getline(iss, cur_line)) {
		if (cur_line.empty()) {
			continue;
		}

		std::istringstream ls(cur_line);
		std::string sl, local, remote, state;
		ls >> sl >> local >> remote >> state;
		if (remote.empty() || state.empty()) {
			continue;
		}

		// Parse remote address (format: "HEX_IP:HEX_PORT").
		auto colon = remote.find(':');
		if (colon == std::string::npos) {
			continue;
		}
		string ip_hex = remote.substr(0, colon);
		string ip = HexToIP(ip_hex);
		++per_remote_ip[ip];
	}
}
} // namespace

unordered_map<string, int> GetTcpConnectionNum() {
	// Maps from IP to TCP connection count.
	unordered_map<string, int> aggregated_tcp_conns;
	ParseProcTCP(IPV4_TCP_PROC_FS_PATH, aggregated_tcp_conns);
	ParseProcTCP(IPV6_TCP_PROC_FS_PATH, aggregated_tcp_conns);
	return aggregated_tcp_conns;
}

#endif

unordered_map<string, int> GetHttpfsTcpConnectionNum() {
	auto all_ips = TcpIpRecorder::GetInstance().GetAllIps();
	const auto tcp_conns = GetTcpConnectionNum();

	unordered_map<string, int> filtered_tcp_conns;
	for (const auto &cur_ip : all_ips) {
		auto iter = tcp_conns.find(cur_ip);
		if (iter == tcp_conns.end()) {
			continue;
		}
		filtered_tcp_conns.emplace(cur_ip, iter->second);
	}
	return filtered_tcp_conns;
}

} // namespace duckdb
