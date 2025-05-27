#include <iostream>
#include <sched.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <cstring>

#define STACK_SIZE (1024 * 1024)

// 容器初始化进程，挂载 proc 并执行用户命令
template<typename... Args>
int container_init(void* arg) {
    char** argv = static_cast<char**>(arg);
    std::cout << "[Container] Running command: " << argv[0] << std::endl;
    // 挂载 proc 文件系统
    if (mount("proc", "/proc", "proc", 0, nullptr) != 0) {
        perror("mount proc failed");
        return -1;
    }
    // 执行用户命令
    if (execvp(argv[0], argv) == -1) {
        perror("execvp failed");
        return -1;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <command> [args...]" << std::endl;
        return 1;
    }
    // 构造命令参数
    char** child_args = &argv[1];
    // 分配子进程栈空间
    char* stack = new char[STACK_SIZE];
    char* stackTop = stack + STACK_SIZE;
    // 使用 clone 创建新命名空间的子进程
    int child_pid = clone(container_init, stackTop, 
        CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS | SIGCHLD, child_args);
    if (child_pid == -1) {
        perror("clone failed");
        delete[] stack;
        return -1;
    }
    // 等待子进程结束
    waitpid(child_pid, nullptr, 0);
    delete[] stack;
    return 0;
}