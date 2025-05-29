#include <iostream>
#include <sched.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <cstring>
#include <sys/stat.h>
#include <fcntl.h>
#include <fstream>
#include <string>

#define STACK_SIZE (1024 * 1024)

// cgroup 路径和名称
const std::string CGROUP_ROOT = "/sys/fs/cgroup";
const std::string CGROUP_NAME = "simple_demo";

// 设置 cgroup 资源限制（内存、cpu.shares、cpuset）
void setup_cgroup(pid_t pid, size_t mem_limit_bytes, const std::string& cpu_shares, const std::string& cpuset) {
    // memory
    std::string mem_path = CGROUP_ROOT + "/memory/" + CGROUP_NAME;
    mkdir(mem_path.c_str(), 0755);
    std::ofstream mem_limit(mem_path + "/memory.limit_in_bytes");
    mem_limit << mem_limit_bytes;
    mem_limit.close();
    std::ofstream mem_procs(mem_path + "/cgroup.procs");
    mem_procs << pid;
    mem_procs.close();
    // cpu.shares
    if (!cpu_shares.empty()) {
        std::string cpu_path = CGROUP_ROOT + "/cpu/" + CGROUP_NAME;
        mkdir(cpu_path.c_str(), 0755);
        std::ofstream cpu_share(cpu_path + "/cpu.shares");
        cpu_share << cpu_shares;
        cpu_share.close();
        std::ofstream cpu_procs(cpu_path + "/cgroup.procs");
        cpu_procs << pid;
        cpu_procs.close();
    }
    // cpuset
    if (!cpuset.empty()) {
        std::string cpuset_path = CGROUP_ROOT + "/cpuset/" + CGROUP_NAME;
        mkdir(cpuset_path.c_str(), 0755);
        // 必须先写入 cpuset.mems，否则 cpuset.cpus 无法设置
        std::ofstream mems(cpuset_path + "/cpuset.mems");
        mems << "0";
        mems.close();
        std::ofstream cpus(cpuset_path + "/cpuset.cpus");
        cpus << cpuset;
        cpus.close();
        std::ofstream cpuset_procs(cpuset_path + "/cgroup.procs");
        cpuset_procs << pid;
        cpuset_procs.close();
    }
}

// 容器初始化进程，挂载 proc 并执行用户命令
template<typename... Args>
int container_init(void* arg) {
    char** argv = static_cast<char**>(arg);
    std::cout << "[Container] Running command: " << argv[0] << std::endl;
    if (mount("proc", "/proc", "proc", 0, nullptr) != 0) {
        perror("mount proc failed");
        return -1;
    }
    if (execvp(argv[0], argv) == -1) {
        perror("execvp failed");
        return -1;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <command> [args...] [--mem <MB>] [--cpu <shares>] [--cpuset <cpus>]" << std::endl;
        return 1;
    }
    // 默认资源限制
    size_t mem_limit = 50 * 1024 * 1024; // 50MB
    std::string cpu_shares = "";
    std::string cpuset = "";
    std::vector<char*> cmd_args;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--mem") == 0 && i + 1 < argc) {
            mem_limit = atoi(argv[++i]) * 1024 * 1024;
        } else if (strcmp(argv[i], "--cpu") == 0 && i + 1 < argc) {
            cpu_shares = argv[++i];
        } else if (strcmp(argv[i], "--cpuset") == 0 && i + 1 < argc) {
            cpuset = argv[++i];
        } else {
            cmd_args.push_back(argv[i]);
        }
    }
    if (cmd_args.empty()) {
        std::cerr << "No command specified" << std::endl;
        return 1;
    }
    cmd_args.push_back(nullptr);
    char** child_args = cmd_args.data();
    char* stack = new char[STACK_SIZE];
    char* stackTop = stack + STACK_SIZE;
    int child_pid = clone(container_init, stackTop, CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS | SIGCHLD, child_args);
    if (child_pid == -1) {
        perror("clone failed");
        delete[] stack;
        return -1;
    }
    // 设置 cgroup 资源限制
    setup_cgroup(child_pid, mem_limit, cpu_shares, cpuset);
    waitpid(child_pid, nullptr, 0);
    delete[] stack;
    return 0;
}