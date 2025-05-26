// 引入必要的头文件
#include <iostream>      // 标准输入输出流库
#include <sched.h>       // clone、CLONE_NEWUTS、CLONE_NEWPID 等命名空间相关的系统调用和宏
#include <sys/wait.h>    // waitpid 等进程等待相关函数
#include <unistd.h>      // gethostname、sethostname 等主机名相关函数
#include <cstring>       // strerror，用于错误信息输出
#include <errno.h>       // errno，全局错误码
#include <fstream>       // 文件操作
#include <string>        // 字符串操作
#include <sys/stat.h>    // mkdir 等文件系统操作
#include <fcntl.h>       // open 等文件操作

// 子进程执行的函数
enum { STACK_SIZE = 1024 * 1024 };

// cgroup 相关路径
const std::string CGROUP_ROOT = "/sys/fs/cgroup";
const std::string CGROUP_NAME = "container_demo";
const std::string MEMORY_LIMIT = "50M";  // 限制内存为 50MB

// 创建 cgroup 目录和设置内存限制
bool setupCgroup() {
    std::string cgroup_path = CGROUP_ROOT + "/memory/" + CGROUP_NAME;
    
    // 创建 cgroup 目录
    if (mkdir(cgroup_path.c_str(), 0755) == -1 && errno != EEXIST) {
        std::cerr << "Failed to create cgroup directory: " << strerror(errno) << std::endl;
        return false;
    }
    
    // 设置内存限制
    // cgroup 路径在大多数标准 Linux 系统（如 Ubuntu、CentOS，cgroups v1）上是确定的，通常为 /sys/fs/cgroup/子系统名/你的cgroup名
    std::string cgroup_path = "/sys/fs/cgroup/memory/" + cgroup_name;
    std::string limit_file = cgroup_path + "/memory.limit_in_bytes";
    std::ofstream limit_stream(limit_file);
    if (!limit_stream.is_open()) {
        std::cerr << "Failed to open memory limit file: " << limit_file << std::endl;
        return false;
    }
    
    // 将 50M 转换为字节数 (50 * 1024 * 1024)
    limit_stream << "52428800" << std::endl;
    limit_stream.close();
    
    std::cout << "[Setup] Created cgroup and set memory limit to " << MEMORY_LIMIT << std::endl;
    return true;
}

// 将进程添加到 cgroup
bool addProcessToCgroup(pid_t pid) {
    std::string cgroup_path = CGROUP_ROOT + "/memory/" + CGROUP_NAME;
    std::string procs_file = cgroup_path + "/cgroup.procs";
    
    std::ofstream procs_stream(procs_file);
    if (!procs_stream.is_open()) {
        std::cerr << "Failed to open cgroup.procs file: " << procs_file << std::endl;
        return false;
    }
    
    procs_stream << pid << std::endl;
    procs_stream.close();
    
    std::cout << "[Setup] Added process " << pid << " to cgroup" << std::endl;
    return true;
}

// 读取当前内存使用情况
void printMemoryUsage() {
    std::string cgroup_path = CGROUP_ROOT + "/memory/" + CGROUP_NAME;
    // memory.usage_in_bytes 文件用于读取当前 cgroup 已使用的内存，路径在 cgroups v1 下是标准的
    std::string usage_file = cgroup_path + "/memory.usage_in_bytes";
    
    std::ifstream usage_stream(usage_file);
    if (usage_stream.is_open()) {
        std::string usage;
        std::getline(usage_stream, usage);
        long usage_bytes = std::stol(usage);
        std::cout << "[Memory] Current usage: " << usage_bytes / 1024 / 1024 << " MB" << std::endl;
        usage_stream.close();
    }
}

// 尝试分配大量内存来测试限制
void testMemoryAllocation() {
    std::cout << "[Test] Starting memory allocation test..." << std::endl;
    
    const size_t chunk_size = 10 * 1024 * 1024; // 每次分配 10MB
    const int max_chunks = 10; // 最多尝试分配 100MB
    
    for (int i = 0; i < max_chunks; i++) {
        try {
            char* memory = new char[chunk_size];
            // 实际写入内存以确保分配
            memset(memory, 'A', chunk_size);
            
            std::cout << "[Test] Allocated chunk " << (i + 1) << " (" 
                      << (i + 1) * 10 << " MB total)" << std::endl;
            
            printMemoryUsage();
            sleep(1); // 暂停观察
            
        } catch (const std::bad_alloc& e) {
            std::cout << "[Test] Memory allocation failed: " << e.what() << std::endl;
            break;
        }
    }
}

// 子进程函数
int childFunc(void *arg) {
    // 设置子进程的主机名（UTS namespace 隔离）
    sethostname("container-host", 14);
    
    // 获取并打印当前主机名和PID
    char hostname[1024];
    gethostname(hostname, sizeof(hostname));
    std::cout << "[Child] Hostname: " << hostname << std::endl;
    std::cout << "[Child] PID: " << getpid() << std::endl;
    
    // 将当前进程添加到 cgroup
    if (!addProcessToCgroup(getpid())) {
        std::cerr << "[Child] Failed to add process to cgroup" << std::endl;
        return 1;
    }
    
    // 打印初始内存使用情况
    printMemoryUsage();
    
    // 测试内存分配
    testMemoryAllocation();
    
    std::cout << "[Child] Container process finished" << std::endl;
    return 0;
}

// 清理 cgroup
void cleanupCgroup() {
    std::string cgroup_path = CGROUP_ROOT + "/memory/" + CGROUP_NAME;
    if (rmdir(cgroup_path.c_str()) == 0) {
        std::cout << "[Cleanup] Removed cgroup directory" << std::endl;
    }
}

int main() {
    std::cout << "=== cgroups Memory Limit Demo ===" << std::endl;
    
    // 打印父进程信息
    char hostname[1024];
    gethostname(hostname, sizeof(hostname));
    std::cout << "[Parent] Hostname: " << hostname << std::endl;
    std::cout << "[Parent] PID: " << getpid() << std::endl;
    
    // 设置 cgroup
    if (!setupCgroup()) {
        std::cerr << "Failed to setup cgroup" << std::endl;
        return 1;
    }
    
    // 为子进程分配栈空间
    char *stack = new char[STACK_SIZE];
    char *stackTop = stack + STACK_SIZE;
    
    // 使用 clone 创建新进程，并指定新建 UTS 和 PID 命名空间
    // 这模拟了容器的隔离环境
    int child_pid = clone(childFunc, stackTop, 
                         CLONE_NEWUTS | CLONE_NEWPID | SIGCHLD, nullptr);
    
    if (child_pid == -1) {
        std::cerr << "clone failed: " << strerror(errno) << std::endl;
        delete[] stack;
        cleanupCgroup();
        return 1;
    }
    
    std::cout << "[Parent] Created container process with PID: " << child_pid << std::endl;
    
    // 等待子进程结束
    int status;
    waitpid(child_pid, &status, 0);
    
    std::cout << "[Parent] Container process exited with status: " << status << std::endl;
    
    // 清理资源
    delete[] stack;
    cleanupCgroup();
    
    return 0;
}