#include "thread_utils.hpp"

#include <pthread.h>
#include <thread>

#if defined(__linux__)
#include <sched.h>
#endif

namespace duckdb {

int GetCpuCoreCount() {
#if defined(__APPLE__)
	return std::thread::hardware_concurrency();
#else
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	sched_getaffinity(0, sizeof(cpuset), &cpuset);
	const int core_count = CPU_COUNT(&cpuset);
	return core_count;
#endif
}

} // namespace duckdb
