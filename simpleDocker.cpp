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
#include <filesystem>
#include <cstdlib>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h> // 用于 makedev()
#include <ctime> // 用于时间函数
#include <cstdio> // 用于printf函数
#include <signal.h> // 用于kill函数和信号
#include <sys/socket.h> // 用于网络操作
#include <netinet/in.h> // 用于网络地址结构
#include <arpa/inet.h> // 用于IP地址转换
#include <net/if.h> // 用于网络接口操作
#include <map>
#include "constants.h"
#include "structures.h"
#include "utils.h"
#include "logging/logging.h"
#include "network/network.h"
#include "container/container.h"
#include "filesystem/filesystem.h"
#include "cgroup/cgroup.h"

// 容器参数结构体
struct ContainerArgs {
    char** child_args;
    std::string log_file_path;
    bool detach_mode;
    std::vector<std::string> env_vars;
    std::string network_name;
    std::vector<std::string> port_mapping;
};

// 容器初始化进程，设置文件系统并执行用户命令
int container_init(void* arg) {
    ContainerArgs* container_args = (ContainerArgs*)arg;
    char** child_args = container_args->child_args;
    
    std::cout << "[Container] Container init process started" << std::endl;
    
    // 在detach模式下，重定向标准输出和标准错误到日志文件
    if (container_args->detach_mode && !container_args->log_file_path.empty()) {
        setup_log_redirection(container_args->log_file_path);
    }

    // 挂载必要的文件系统
    setup_mount();
    
    // 设置环境变量
    for (const auto& env_var : container_args->env_vars) {
        std::cout << "[Container] Setting environment variable: " << env_var << std::endl;
        if (putenv(strdup(env_var.c_str())) != 0) {
            std::cerr << "[Container] Failed to set environment variable: " << env_var << std::endl;
        }
    }
    
    std::cout << "[Container] Executing command: " << child_args[0] << std::endl;
    
    // 执行用户指定的命令
    if (execvp(child_args[0], child_args) != 0) {
        perror("execvp failed");
        return -1;
    }
    
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <command> [args...] [--mem <MB>] [--cpu <shares>] [--cpuset <cpus>] [-v <host_path:container_path>] [-e <key=value>] [--net <network_name>] [-p <host_port:container_port>] [--commit <image_name>] [--name <container_name>] [-d]" << std::endl;
        std::cerr << "       " << argv[0] << " ps" << std::endl;
        std::cerr << "       " << argv[0] << " logs <container_name>" << std::endl;
        std::cerr << "       " << argv[0] << " exec <container_name> <command> [args...]" << std::endl;
        std::cerr << "       " << argv[0] << " stop <container_name>" << std::endl;
        std::cerr << "       " << argv[0] << " rm <container_name>" << std::endl;
        std::cerr << "       " << argv[0] << " network create --driver <driver> --subnet <subnet> <name>" << std::endl;
        std::cerr << "       " << argv[0] << " network list" << std::endl;
        std::cerr << "       " << argv[0] << " network remove <name>" << std::endl;
        std::cerr << "Example: " << argv[0] << " /bin/sh --mem 100 --cpu 512 --cpuset 0-1 -v /tmp:/tmp -e MY_VAR=hello --net testbr0 -p 8080:80 --name mycontainer" << std::endl;
        std::cerr << "Detach:  " << argv[0] << " /bin/sh -d --name mycontainer" << std::endl;
        std::cerr << "Commit:  " << argv[0] << " /bin/sh --commit myimage" << std::endl;
        std::cerr << "Exec:    " << argv[0] << " exec mycontainer /bin/ls" << std::endl;
        std::cerr << "Stop:    " << argv[0] << " stop mycontainer" << std::endl;
        std::cerr << "Remove:  " << argv[0] << " rm mycontainer" << std::endl;
        return 1;
    }
    
    // 处理ps命令
    if (argc == 2 && strcmp(argv[1], "ps") == 0) {
        list_containers();
        return 0;
    }
    
    // 处理logs命令
    if (argc == 3 && strcmp(argv[1], "logs") == 0) {
        show_container_logs(argv[2]);
        return 0;
    }
    
    // 处理exec命令
    if (argc >= 4 && strcmp(argv[1], "exec") == 0) {
        std::string container_name = argv[2];
        std::vector<std::string> exec_cmd;
        for (int i = 3; i < argc; ++i) {
            exec_cmd.push_back(argv[i]);
        }
        exec_container(container_name, exec_cmd);
        return 0;
    }
    
    // 处理stop命令
    if (argc == 3 && strcmp(argv[1], "stop") == 0) {
        stop_container(argv[2]);
        return 0;
    }
    
    // 处理rm命令
    if (argc == 3 && strcmp(argv[1], "rm") == 0) {
        remove_container(argv[2]);
        return 0;
    }
    
    // 处理network命令
    if (argc >= 3 && strcmp(argv[1], "network") == 0) {
        if (strcmp(argv[2], "create") == 0) {
            if (argc < 4) {
                std::cerr << "Usage: " << argv[0] << " network create --driver <driver> --subnet <subnet> <name>" << std::endl;
                return 1;
            }
            
            std::string driver = "bridge";
            std::string subnet = "192.168.1.0/24";
            std::string name = "";
            
            // 解析网络创建参数
            for (int i = 3; i < argc; ++i) {
                if (strcmp(argv[i], "--driver") == 0 && i + 1 < argc) {
                    driver = argv[++i];
                } else if (strcmp(argv[i], "--subnet") == 0 && i + 1 < argc) {
                    subnet = argv[++i];
                } else {
                    name = argv[i];
                }
            }
            
            if (name.empty()) {
                std::cerr << "Network name is required" << std::endl;
                return 1;
            }
            
            network_create(driver, subnet, name);
            return 0;
        } else if (strcmp(argv[2], "list") == 0) {
            network_list();
            return 0;
        } else if (strcmp(argv[2], "remove") == 0) {
            if (argc < 4) {
                std::cerr << "Usage: " << argv[0] << " network remove <name>" << std::endl;
                return 1;
            }
            network_remove(argv[3]);
            return 0;
        } else {
            std::cerr << "Unknown network command: " << argv[2] << std::endl;
            return 1;
        }
    }
    
    std::cout << "[Main] Starting SimpleDocker with filesystem isolation..." << std::endl;
    
    // 默认资源限制
    size_t mem_limit = 50 * 1024 * 1024; // 50MB
    std::string cpu_shares = "";
    std::string cpuset = "";
    std::string volume_str = "";
    std::string commit_image = "";
    std::string container_name = "";
    bool detach_mode = false;
    std::vector<std::string> env_vars;
    std::string network_name = "";
    std::vector<std::string> port_mapping;
    std::vector<char*> cmd_args;
    
    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--mem") == 0 && i + 1 < argc) {
            mem_limit = atoi(argv[++i]) * 1024 * 1024;
        } else if (strcmp(argv[i], "--cpu") == 0 && i + 1 < argc) {
            cpu_shares = argv[++i];
        } else if (strcmp(argv[i], "--cpuset") == 0 && i + 1 < argc) {
            cpuset = argv[++i];
        } else if (strcmp(argv[i], "-v") == 0 && i + 1 < argc) {
            volume_str = argv[++i];
        } else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
            env_vars.push_back(argv[++i]);
        } else if (strcmp(argv[i], "--net") == 0 && i + 1 < argc) {
            network_name = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port_mapping.push_back(argv[++i]);
        } else if (strcmp(argv[i], "--commit") == 0 && i + 1 < argc) {
            commit_image = argv[++i];
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            container_name = argv[++i];
        } else if (strcmp(argv[i], "-d") == 0) {
            detach_mode = true;
        } else {
            cmd_args.push_back(argv[i]);
        }
    }
    
    if (cmd_args.empty()) {
        std::cerr << "[Error] No command specified" << std::endl;
        return 1;
    }
    cmd_args.push_back(nullptr);
    char** child_args = cmd_args.data();
    
    // 解析volume参数
    VolumeInfo volume_info;
    if (!volume_str.empty()) {
        volume_info = parse_volume(volume_str);
    } else {
        volume_info = {"", "", false};
    }
    
    // 生成容器ID和名称
    std::string container_id = generate_container_id();
    if (container_name.empty()) {
        container_name = container_id;
    }
    
    std::cout << "[Main] Container ID: " << container_id << std::endl;
    std::cout << "[Main] Container Name: " << container_name << std::endl;
    
    // 创建容器工作空间（OverlayFS文件系统）
    new_workspace(volume_info);
    
    // 准备容器参数
    ContainerArgs container_args;
    container_args.child_args = child_args;
    container_args.detach_mode = detach_mode;
    container_args.env_vars = env_vars;
    container_args.network_name = network_name;
    container_args.port_mapping = port_mapping;
    
    // 构建日志文件路径
    std::string log_file_path = CONTAINER_INFO_PATH + container_name + "/" + CONTAINER_LOG_FILE;
    container_args.log_file_path = log_file_path;
    
    // 创建容器进程
    char* stack = new char[STACK_SIZE];
    char* stackTop = stack + STACK_SIZE;
    
    std::cout << "[Main] Creating container process..." << std::endl;
    int child_pid = clone(container_init, stackTop, 
                         CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS | 
                         CLONE_NEWNET | CLONE_NEWIPC | SIGCHLD, 
                         &container_args);
    
    if (child_pid == -1) {
        perror("clone failed");
        delete[] stack;
        delete_workspace();
        return -1;
    }
    
    std::cout << "[Main] Container process created with PID: " << child_pid << std::endl;
    
    // 记录容器信息
    std::vector<std::string> command_vector;
    for (char** arg = child_args; *arg != nullptr; ++arg) {
        command_vector.push_back(*arg);
    }
    
    std::string recorded_name = record_container_info(child_pid, command_vector, container_name, container_id);
    if (recorded_name.empty()) {
        std::cerr << "[Main] Failed to record container info" << std::endl;
    }
    
    // 设置 cgroup 资源限制
    setup_cgroup(child_pid, mem_limit, cpu_shares, cpuset);
    
    // 配置网络（如果指定了网络）
    if (!network_name.empty()) {
        // 确保默认网络存在
        if (network_name == DEFAULT_BRIDGE_NAME) {
            create_bridge_network(DEFAULT_BRIDGE_NAME, DEFAULT_SUBNET);
        }
        
        // 分配IP地址
        NetworkInfo network = load_network_config(network_name);
        if (network.name.empty()) {
            std::cerr << "[Network] Network not found: " << network_name << std::endl;
        } else {
            std::string container_ip = allocate_ip(network.ip_range);
            if (!container_ip.empty()) {
                // 设置容器网络
                if (setup_container_network(container_id, network_name, container_ip, child_pid)) {
                    // 配置端口映射
                    if (!port_mapping.empty()) {
                        setup_port_mapping(container_ip, port_mapping);
                    }
                    std::cout << "[Network] Container IP: " << container_ip << std::endl;
                } else {
                    std::cerr << "[Network] Failed to setup container network" << std::endl;
                }
            } else {
                std::cerr << "[Network] Failed to allocate IP address" << std::endl;
            }
        }
    }
    
    if (detach_mode) {
        // Detach模式：不等待容器进程结束，直接返回
        std::cout << "[Main] Container started in detach mode with PID: " << child_pid << std::endl;
        std::cout << "[Main] Container Name: " << container_name << std::endl;
        std::cout << "[Main] Container is running in background" << std::endl;
        
        // 在detach模式下不清理资源，让容器继续运行
        delete[] stack;
        std::cout << "[Main] SimpleDocker detached successfully" << std::endl;
        std::cout << "[Main] Use 'ps' to list containers and 'logs " << container_name << "' to view logs" << std::endl;
        return 0;
    } else {
        // 非detach模式：等待容器进程结束
        std::cout << "[Main] Waiting for container to finish..." << std::endl;
        int status;
        waitpid(child_pid, &status, 0);
        
        std::cout << "[Main] Container finished with status: " << WEXITSTATUS(status) << std::endl;
        
        // 如果指定了commit，则保存容器为镜像
        if (!commit_image.empty()) {
            commit_container(commit_image);
        }
        
        // 删除容器信息（非detach模式下容器已结束）
        delete_container_info(container_name);
        
        // 清理资源
        std::cout << "[Main] Cleaning up resources..." << std::endl;
        delete[] stack;
        delete_workspace(volume_info);
        
        std::cout << "[Main] SimpleDocker finished successfully" << std::endl;
        return 0;
    }
}