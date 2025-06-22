#include "cgroup.h"
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include "common/constants.h"

// 设置 cgroup 资源限制（内存、cpu.shares、cpuset）
void setup_cgroup(pid_t pid, size_t mem_limit_bytes, const std::string& cpu_shares, const std::string& cpuset) {
    std::cout << "[CGroup] Setting up resource limits for PID " << pid << std::endl;
    
    // memory
    std::string mem_path = CGROUP_ROOT + "/memory/" + CGROUP_NAME;
    mkdir(mem_path.c_str(), 0755);
    std::ofstream mem_limit(mem_path + "/memory.limit_in_bytes");
    mem_limit << mem_limit_bytes;
    mem_limit.close();
    std::ofstream mem_procs(mem_path + "/cgroup.procs");
    mem_procs << pid;
    mem_procs.close();
    std::cout << "[CGroup] Memory limit set to " << mem_limit_bytes / (1024*1024) << "MB" << std::endl;
    
    // cpu.shares - CPU权重控制（软限制） 示例：1024
    // 作用：设置进程相对于其他进程的CPU使用权重，不是绝对限制
    // 特点：当CPU空闲时可以使用更多资源，竞争时按权重分配
    if (!cpu_shares.empty()) {
        std::string cpu_path = CGROUP_ROOT + "/cpu/" + CGROUP_NAME;
        mkdir(cpu_path.c_str(), 0755);
        std::ofstream cpu_share(cpu_path + "/cpu.shares");
        cpu_share << cpu_shares;  // 默认1024，设置512表示获得一半CPU时间
        cpu_share.close();
        std::ofstream cpu_procs(cpu_path + "/cgroup.procs");
        cpu_procs << pid;
        cpu_procs.close();
        std::cout << "[CGroup] CPU shares set to " << cpu_shares << std::endl;
    }
    
    // cpuset - CPU核心绑定（硬限制） 示例："0" "0,2" "0-3"
    // 作用：将进程绑定到指定的CPU核心上运行
    // 特点：硬性限制，进程只能在指定核心上运行，提高缓存命中率
    if (!cpuset.empty()) {
        std::string cpuset_path = CGROUP_ROOT + "/cpuset/" + CGROUP_NAME;
        mkdir(cpuset_path.c_str(), 0755);
        // 必须先写入 cpuset.mems，否则 cpuset.cpus 无法设置
        std::ofstream mems(cpuset_path + "/cpuset.mems");
        mems << "0";  // 指定内存节点，通常设为0（第一个NUMA节点）
        mems.close();
        std::ofstream cpus(cpuset_path + "/cpuset.cpus");
        cpus << cpuset;  // 格式："0"(单核) "0,2"(多核) "0-3"(范围)
        cpus.close();
        std::ofstream cpuset_procs(cpuset_path + "/cgroup.procs");
        cpuset_procs << pid;
        cpuset_procs.close();
        std::cout << "[CGroup] CPU set to " << cpuset << std::endl;
    }
}