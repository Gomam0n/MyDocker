#include "container.h"
#include "../common/constants.h"
#include "../logging/logging.h"
#include "../common/utils.h"
#include "../network/network.h"
#include <iostream>
#include <fstream>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <sched.h>
#include <cstring>

// 记录容器信息
std::string record_container_info(pid_t container_pid, const std::vector<std::string>& command_array, 
                                  const std::string& container_name, const std::string& container_id) {
    std::cout << "[Container] Recording container info..." << std::endl;
    
    // 获取当前时间
    time_t now = time(0);
    char* time_str = ctime(&now);
    std::string created_time(time_str);
    created_time.pop_back(); // 移除换行符
    
    // 构建命令字符串
    std::string command = "";
    for (const auto& arg : command_array) {
        command += arg + " ";
    }
    if (!command.empty()) {
        command.pop_back(); // 移除最后的空格
    }
    
    // 创建容器信息
    ContainerInfo container_info;
    container_info.id = container_id;
    container_info.name = container_name;
    container_info.pid = std::to_string(container_pid);
    container_info.command = command;
    container_info.created_time = created_time;
    container_info.status = RUNNING;
    
    // 创建容器信息目录
    std::string dir_path = CONTAINER_INFO_PATH + container_name + "/";
    if (!create_directory_if_not_exists(dir_path)) {
        return "";
    }
    
    // 写入配置文件（简化的JSON格式）
    std::string config_file = dir_path + CONFIG_NAME;
    std::ofstream config_stream(config_file);
    if (config_stream.is_open()) {
        config_stream << "{\n";
        config_stream << "  \"id\": \"" << container_info.id << "\",\n";
        config_stream << "  \"name\": \"" << container_info.name << "\",\n";
        config_stream << "  \"pid\": \"" << container_info.pid << "\",\n";
        config_stream << "  \"command\": \"" << container_info.command << "\",\n";
        config_stream << "  \"createTime\": \"" << container_info.created_time << "\",\n";
        config_stream << "  \"status\": \"" << container_info.status << "\"\n";
        config_stream << "}\n";
        config_stream.close();
        std::cout << "[Container] Container info recorded: " << config_file << std::endl;
    } else {
        std::cerr << "[Container] Failed to create config file" << std::endl;
        return "";
    }
    
    // 创建空的日志文件
    create_container_log_file(dir_path);
    
    return container_name;
}

// 删除容器信息
void delete_container_info(const std::string& container_name) {
    std::cout << "[Container] Deleting container info: " << container_name << std::endl;
    
    std::string dir_path = CONTAINER_INFO_PATH + container_name;
    std::string rm_cmd = "rm -rf " + dir_path;
    
    if (system(rm_cmd.c_str()) != 0) {
        std::cerr << "[Container] Failed to delete container info directory" << std::endl;
    } else {
        std::cout << "[Container] Container info deleted successfully" << std::endl;
    }
}

// 解析容器配置文件
ContainerInfo parse_container_config(const std::string& config_file) {
    ContainerInfo container_info;
    std::ifstream file(config_file);
    
    if (!file.is_open()) {
        std::cerr << "[Container] Failed to open config file: " << config_file << std::endl;
        return container_info;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // 简单的JSON解析（仅适用于我们的格式）
        if (line.find("\"id\":") != std::string::npos) {
            size_t start = line.find('"', line.find(':')) + 1;
            size_t end = line.find('"', start);
            container_info.id = line.substr(start, end - start);
        } else if (line.find("\"name\":") != std::string::npos) {
            size_t start = line.find('"', line.find(':')) + 1;
            size_t end = line.find('"', start);
            container_info.name = line.substr(start, end - start);
        } else if (line.find("\"pid\":") != std::string::npos) {
            size_t start = line.find('"', line.find(':')) + 1;
            size_t end = line.find('"', start);
            container_info.pid = line.substr(start, end - start);
        } else if (line.find("\"command\":") != std::string::npos) {
            size_t start = line.find('"', line.find(':')) + 1;
            size_t end = line.find('"', start);
            container_info.command = line.substr(start, end - start);
        } else if (line.find("\"createTime\":") != std::string::npos) {
            size_t start = line.find('"', line.find(':')) + 1;
            size_t end = line.find('"', start);
            container_info.created_time = line.substr(start, end - start);
        } else if (line.find("\"status\":") != std::string::npos) {
            size_t start = line.find('"', line.find(':')) + 1;
            size_t end = line.find('"', start);
            container_info.status = line.substr(start, end - start);
        }
    }
    
    file.close();
    return container_info;
}

// 列出所有容器
void list_containers() {
    std::cout << "[Container] Listing all containers..." << std::endl;
    
    // 检查容器信息目录是否存在
    if (!path_exists(CONTAINER_INFO_PATH)) {
        std::cout << "No containers found." << std::endl;
        return;
    }
    
    // 使用ls命令获取目录列表
    std::string ls_cmd = "ls " + CONTAINER_INFO_PATH + " 2>/dev/null";
    FILE* pipe = popen(ls_cmd.c_str(), "r");
    if (!pipe) {
        std::cerr << "[Container] Failed to list container directories" << std::endl;
        return;
    }
    
    std::vector<ContainerInfo> containers;
    char buffer[256];
    
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string container_name(buffer);
        container_name.pop_back(); // 移除换行符
        
        std::string config_file = CONTAINER_INFO_PATH + container_name + "/" + CONFIG_NAME;
        if (path_exists(config_file)) {
            ContainerInfo info = parse_container_config(config_file);
            if (!info.id.empty()) {
                containers.push_back(info);
            }
        }
    }
    
    pclose(pipe);
    
    // 打印容器列表
    if (containers.empty()) {
        std::cout << "No containers found." << std::endl;
        return;
    }
    
    printf("%-12s %-12s %-8s %-10s %-20s %-20s\n", 
           "ID", "NAME", "PID", "STATUS", "COMMAND", "CREATED");
    printf("%-12s %-12s %-8s %-10s %-20s %-20s\n", 
           "------------", "------------", "--------", "----------", "--------------------", "--------------------");
    
    for (const auto& container : containers) {
        printf("%-12s %-12s %-8s %-10s %-20s %-20s\n",
               container.id.substr(0, 12).c_str(),
               container.name.c_str(),
               container.pid.c_str(),
               container.status.c_str(),
               container.command.substr(0, 20).c_str(),
               container.created_time.c_str());
    }
}

// 获取容器PID
std::string get_container_pid(const std::string& container_name) {
    std::string config_file = CONTAINER_INFO_PATH + container_name + "/" + CONFIG_NAME;
    
    if (!path_exists(config_file)) {
        std::cerr << "[Exec] Container config file not found: " << config_file << std::endl;
        return "";
    }
    
    ContainerInfo container_info = parse_container_config(config_file);
    if (container_info.pid.empty()) {
        std::cerr << "[Exec] Failed to get container PID" << std::endl;
        return "";
    }
    
    return container_info.pid;
}

// 获取容器的环境变量
std::vector<std::string> get_container_envs(const std::string& container_pid) {
    std::vector<std::string> envs;
    std::string environ_path = "/proc/" + container_pid + "/environ";
    
    std::ifstream environ_file(environ_path, std::ios::binary);
    if (!environ_file.is_open()) {
        std::cerr << "[Exec] Failed to open environ file: " << environ_path << std::endl;
        return envs;
    }
    
    std::string content((std::istreambuf_iterator<char>(environ_file)),
                        std::istreambuf_iterator<char>());
    environ_file.close();
    
    // 按\0分割环境变量
    size_t start = 0;
    size_t pos = 0;
    while ((pos = content.find('\0', start)) != std::string::npos) {
        if (pos > start) {
            std::string env_var = content.substr(start, pos - start);
            if (!env_var.empty()) {
                envs.push_back(env_var);
            }
        }
        start = pos + 1;
    }
    
    // 处理最后一个环境变量（如果没有以\0结尾）
    if (start < content.length()) {
        std::string env_var = content.substr(start);
        if (!env_var.empty()) {
            envs.push_back(env_var);
        }
    }
    
    std::cout << "[Exec] Found " << envs.size() << " environment variables" << std::endl;
    return envs;
}

// 进入容器命名空间执行命令
void exec_container(const std::string& container_name, const std::vector<std::string>& exec_cmd) {
    std::cout << "[Exec] Executing command in container: " << container_name << std::endl;
    
    // 获取容器PID
    std::string container_pid = get_container_pid(container_name);
    if (container_pid.empty()) {
        std::cerr << "[Exec] Failed to get container PID" << std::endl;
        return;
    }
    
    std::cout << "[Exec] Container PID: " << container_pid << std::endl;
    
    // 构建命令字符串
    std::string cmd_str = "";
    for (size_t i = 0; i < exec_cmd.size(); ++i) {
        cmd_str += exec_cmd[i];
        if (i < exec_cmd.size() - 1) {
            cmd_str += " ";
        }
    }
    
    std::cout << "[Exec] Command: " << cmd_str << std::endl;
    
    // 进入容器的各个命名空间
    std::vector<std::string> namespaces = {"ipc", "uts", "net", "pid", "mnt"};
    
    for (const auto& ns : namespaces) {
        std::string ns_path = "/proc/" + container_pid + "/ns/" + ns;
        int fd = open(ns_path.c_str(), O_RDONLY);
        
        if (fd == -1) {
            std::cerr << "[Exec] Failed to open namespace: " << ns_path << std::endl;
            continue;
        }
        
        if (setns(fd, 0) == -1) {
            std::cerr << "[Exec] Failed to enter namespace: " << ns << std::endl;
            perror("setns");
        } else {
            std::cout << "[Exec] Entered namespace: " << ns << std::endl;
        }
        
        close(fd);
    }

    // 切换到容器的根目录
    if (chdir("/") != 0) {
        perror("[Exec] Failed to chdir to container root");
        return;
    }

    std::cout << "[Exec] Changed working directory to container root" << std::endl;
    
    // 获取容器的环境变量
    std::vector<std::string> container_envs = get_container_envs(container_pid);
    
    // 设置容器的环境变量
    for (const auto& env_var : container_envs) {
        std::cout << "[Exec] Setting environment variable: " << env_var << std::endl;
        if (putenv(strdup(env_var.c_str())) != 0) {
            std::cerr << "[Exec] Failed to set environment variable: " << env_var << std::endl;
        }
    }
    
    // 执行命令
    std::vector<char*> args;
    for (const auto& arg : exec_cmd) {
        args.push_back(const_cast<char*>(arg.c_str()));
    }
    args.push_back(nullptr);
    
    if (execvp(args[0], args.data()) == -1) {
        perror("[Exec] execvp failed");
    }
}

// 停止容器
void stop_container(const std::string& container_name) {
    std::cout << "[Stop] Stopping container: " << container_name << std::endl;
    
    // 获取容器PID
    std::string container_pid = get_container_pid(container_name);
    if (container_pid.empty()) {
        std::cerr << "[Stop] Failed to get container PID" << std::endl;
        return;
    }
    
    std::cout << "[Stop] Container PID: " << container_pid << std::endl;
    
    // 转换PID为整数
    pid_t pid = std::stoi(container_pid);
    
    // 发送SIGTERM信号停止容器
    if (kill(pid, SIGTERM) == -1) {
        perror("[Stop] Failed to stop container");
        return;
    }
    
    std::cout << "[Stop] SIGTERM signal sent to container" << std::endl;
    
    // 更新容器状态为stopped
    ContainerInfo container_info = parse_container_config(CONTAINER_INFO_PATH + container_name + "/" + CONFIG_NAME);
    if (container_info.id.empty()) {
        std::cerr << "[Stop] Failed to read container info" << std::endl;
        return;
    }
    
    container_info.status = STOPPED;
    container_info.pid = "";
    
    // 写回配置文件
    std::string config_file = CONTAINER_INFO_PATH + container_name + "/" + CONFIG_NAME;
    std::ofstream config_stream(config_file);
    if (config_stream.is_open()) {
        config_stream << "{\n";
        config_stream << "  \"id\": \"" << container_info.id << "\",\n";
        config_stream << "  \"name\": \"" << container_info.name << "\",\n";
        config_stream << "  \"pid\": \"" << container_info.pid << "\",\n";
        config_stream << "  \"command\": \"" << container_info.command << "\",\n";
        config_stream << "  \"createTime\": \"" << container_info.created_time << "\",\n";
        config_stream << "  \"status\": \"" << container_info.status << "\"\n";
        config_stream << "}\n";
        config_stream.close();
        std::cout << "[Stop] Container status updated to stopped" << std::endl;
    } else {
        std::cerr << "[Stop] Failed to update container config" << std::endl;
    }
}

// 删除容器
void remove_container(const std::string& container_name) {
    std::cout << "[Remove] Removing container: " << container_name << std::endl;
    
    // 检查容器状态
    ContainerInfo container_info = parse_container_config(CONTAINER_INFO_PATH + container_name + "/" + CONFIG_NAME);
    if (container_info.id.empty()) {
        std::cerr << "[Remove] Container not found: " << container_name << std::endl;
        return;
    }
    
    if (container_info.status == RUNNING) {
        std::cerr << "[Remove] Cannot remove running container. Please stop it first." << std::endl;
        return;
    }
    
    // 清理网络资源
    std::string veth_host = "veth" + container_info.id.substr(0, 5);
    if (interface_exists(veth_host)) {
        std::string delete_veth_cmd = "ip link delete " + veth_host;
        if (system(delete_veth_cmd.c_str()) == 0) {
            std::cout << "[Remove] Cleaned up network interface: " << veth_host << std::endl;
        } else {
            std::cerr << "[Remove] Failed to clean up network interface: " << veth_host << std::endl;
        }
    }
    
    // 释放容器IP地址（从容器配置中读取网络信息）
    // Todo: 注意：这里需要扩展容器配置以包含网络信息，目前简化处理
    // 在实际实现中，应该在容器配置中保存网络名称和分配的IP
    std::cout << "[Remove] Note: IP address cleanup requires container network info in config" << std::endl;
    
    // 删除容器信息目录
    std::string container_dir = CONTAINER_INFO_PATH + container_name;
    std::string rm_cmd = "rm -rf " + container_dir;
    
    if (system(rm_cmd.c_str()) == 0) {
        std::cout << "[Remove] Container removed successfully: " << container_name << std::endl;
    } else {
        std::cerr << "[Remove] Failed to remove container directory" << std::endl;
    }
}

// Commit功能：将容器保存为镜像
void commit_container(const std::string& image_name) {
    std::cout << "[Commit] Committing container to image: " << image_name << std::endl;
    
    std::string image_tar = ROOT_URL + image_name + ".tar";
    std::string tar_cmd = "tar -czf " + image_tar + " -C " + MNT_URL + " .";
    
    std::cout << "[Commit] Creating tar archive: " << image_tar << std::endl;
    
    if (system(tar_cmd.c_str()) != 0) {
        std::cerr << "[Commit] Failed to create tar archive" << std::endl;
    } else {
        std::cout << "[Commit] Container committed successfully to: " << image_tar << std::endl;
    }
}