// 引入必要的头文件
#include <iostream>      // 标准输入输出流库
#include <sched.h>       // clone、CLONE_NEWUTS、CLONE_NEWPID 等命名空间相关的系统调用和宏
#include <sys/wait.h>    // waitpid 等进程等待相关函数
#include <unistd.h>      // gethostname、sethostname 等主机名相关函数
#include <cstring>       // strerror，用于错误信息输出
#include <errno.h>       // errno，全局错误码

// 子进程执行的函数
enum { STACK_SIZE = 1024 * 1024 };

// 子进程函数，参数为 void*，返回 int
int childFunc(void *arg) {
    // 设置子进程的主机名（UTS namespace 隔离）
    sethostname("child-host", 10); // sethostname 是系统调用，用于设置当前命名空间下的主机名

    // 获取并打印当前主机名
    char hostname[1024];
    gethostname(hostname, sizeof(hostname)); // gethostname 是库函数，底层调用系统调用获取主机名
    std::cout << "[Child] Hostname: " << hostname << std::endl;

    // 打印当前进程的 PID
    std::cout << "[Child] PID: " << getpid() << std::endl;

    // 让子进程保持一段时间，便于观察
    sleep(2);
    return 0;
}

int main() {
    char hostname[1024];
    gethostname(hostname, sizeof(hostname));
    std::cout << "[Parent] Hostname: " << hostname << std::endl;
    std::cout << "[Parent] PID: " << getpid() << std::endl;

    // 为子进程分配栈空间
    char *stack = new char[STACK_SIZE];
    char *stackTop = stack + STACK_SIZE;

    // 使用 clone 创建新进程，并指定新建 UTS 和 PID 命名空间
    // clone 是 Linux 系统调用，可通过 flags 参数指定命名空间隔离等特性
    int child_pid = clone(childFunc, stackTop, CLONE_NEWUTS | CLONE_NEWPID | SIGCHLD, nullptr);
    if (child_pid == -1) {
        std::cerr << "clone failed: " << strerror(errno) << std::endl;
        delete[] stack;
        return 1;
    }

    // 等待子进程结束
    waitpid(child_pid, nullptr, 0);
    delete[] stack;
    return 0;
}