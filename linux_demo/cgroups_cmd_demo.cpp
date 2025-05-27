// TODO: Make this more interactive
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>
#include <errno.h>
#include <sys/wait.h>

// 用于模拟 cgroup 限制并让子进程接受命令行参数测试内存/CPU
// 用法示例： ./cgroups_cmd_demo mem 100  # 申请100MB内存
//           ./cgroups_cmd_demo cpu 5    # 持续消耗CPU 5秒

const std::string CGROUP_ROOT = "/sys/fs/cgroup";
const std::string CGROUP_NAME = "cmd_demo";

void setup_cgroup(pid_t pid, size_t mem_limit_bytes) {
    std::string cgroup_path = CGROUP_ROOT + "/memory/" + CGROUP_NAME;
    mkdir(cgroup_path.c_str(), 0755);
    // 设置内存限制
    std::ofstream limit_file(cgroup_path + "/memory.limit_in_bytes");
    limit_file << mem_limit_bytes;
    limit_file.close();
    // 将进程加入 cgroup
    std::ofstream procs_file(cgroup_path + "/cgroup.procs");
    procs_file << pid;
    procs_file.close();
}

void child_task(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "用法: ./cgroups_cmd_demo [mem|cpu] [参数]" << std::endl;
        exit(1);
    }
    std::string mode = argv[1];
    if (mode == "mem") {
        // 申请指定MB内存
        int mb = atoi(argv[2]);
        size_t bytes = mb * 1024 * 1024;
        char* p = (char*)malloc(bytes);
        if (p == nullptr) {
            std::cerr << "分配内存失败" << std::endl;
            exit(1);
        }
        memset(p, 1, bytes);
        std::cout << "已分配 " << mb << " MB 内存，按任意键释放..." << std::endl;
        getchar();
        free(p);
    } else if (mode == "cpu") {
        // 持续消耗CPU指定秒数
        int seconds = atoi(argv[2]);
        std::cout << "开始消耗CPU " << seconds << " 秒..." << std::endl;
        time_t start = time(nullptr);
        while (time(nullptr) - start < seconds) {
            for (volatile int i = 0; i < 1000000; ++i);
        }
        std::cout << "CPU 消耗结束" << std::endl;
    } else {
        std::cerr << "未知模式: " << mode << std::endl;
        exit(1);
    }
}

int main(int argc, char* argv[]) {
    size_t mem_limit = 50 * 1024 * 1024; // 默认限制50MB
    if (argc >= 4 && std::string(argv[1]) == "mem") {
        mem_limit = atoi(argv[3]) * 1024 * 1024;
    }
    pid_t pid = fork();
    if (pid == 0) {
        // 子进程：执行测试任务
        child_task(argc, argv);
        return 0;
    } else if (pid > 0) {
        // 父进程：设置cgroup限制
        setup_cgroup(pid, mem_limit);
        waitpid(pid, nullptr, 0);
        // 清理cgroup
        std::string cgroup_path = CGROUP_ROOT + "/memory/" + CGROUP_NAME;
        rmdir(cgroup_path.c_str());
    } else {
        std::cerr << "fork失败" << std::endl;
        return 1;
    }
    return 0;
}