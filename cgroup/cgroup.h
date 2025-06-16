#ifndef CGROUP_H
#define CGROUP_H

#include <string>
#include <sys/types.h>

// cgroup资源限制管理
void setup_cgroup(pid_t pid, size_t mem_limit_bytes, const std::string& cpu_shares, const std::string& cpuset);

#endif // CGROUP_H