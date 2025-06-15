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

#define STACK_SIZE (1024 * 1024)

// cgroup 路径和名称
const std::string CGROUP_ROOT = "/sys/fs/cgroup";
const std::string CGROUP_NAME = "simple_demo";

// 文件系统路径配置
const std::string ROOT_URL = "/home/qianyifan/";
const std::string MNT_URL = "/home/qianyifan/mnt/";
const std::string BUSYBOX_URL = "/home/qianyifan/busybox/";
const std::string BUSYBOX_TAR_URL = "/home/qianyifan/busybox.tar";
const std::string WRITE_LAYER_URL = "/home/qianyifan/writeLayer/";
const std::string WORK_DIR_URL = "/home/qianyifan/work/";

// 容器信息存储路径
const std::string CONTAINER_INFO_PATH = "/var/run/mydocker/";
const std::string CONFIG_NAME = "config.json";
const std::string CONTAINER_LOG_FILE = "container.log";

// 容器状态
const std::string RUNNING = "running";
const std::string STOPPED = "stopped";
const std::string EXITED = "exited";

// 网络相关常量
const std::string DEFAULT_NETWORK_PATH = "/var/run/mydocker/network/network/";
const std::string IPAM_DEFAULT_ALLOCATOR_PATH = "/var/run/mydocker/network/ipam/subnet.json";
const std::string DEFAULT_BRIDGE_NAME = "mydocker0";
const std::string DEFAULT_SUBNET = "192.168.1.0/24";

// Volume相关结构
struct VolumeInfo {
    std::string host_path;
    std::string container_path;
    bool valid;
};

// 网络相关结构
struct NetworkInfo {
    std::string name;
    std::string ip_range;
    std::string driver;
};

struct EndpointInfo {
    std::string id;
    std::string ip_address;
    std::string mac_address;
    std::string network_name;
    std::vector<std::string> port_mapping;
};

struct IPAMInfo {
    std::string subnet;
    std::string allocation_map;
};

// 容器信息结构
struct ContainerInfo {
    std::string id;
    std::string name;
    std::string pid;
    std::string command;
    std::string created_time;
    std::string status;
};

// 生成随机字符串作为容器ID
std::string generate_container_id(int length = 10) {
    const std::string chars = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::string result;
    srand(time(nullptr));
    for (int i = 0; i < length; ++i) {
        result += chars[rand() % chars.length()];
    }
    return result;
}

// 生成唯一的MAC地址
std::string generate_unique_mac(const std::string& container_id) {
    // 使用容器ID的哈希值生成MAC地址
    std::hash<std::string> hasher;
    size_t hash_value = hasher(container_id);
    
    // 生成MAC地址，格式为 02:xx:xx:xx:xx:xx
    // 02开头表示本地管理的单播地址
    char mac[18];
    snprintf(mac, sizeof(mac), "02:%02x:%02x:%02x:%02x:%02x",
             (unsigned char)((hash_value >> 32) & 0xFF),
             (unsigned char)((hash_value >> 24) & 0xFF),
             (unsigned char)((hash_value >> 16) & 0xFF),
             (unsigned char)((hash_value >> 8) & 0xFF),
             (unsigned char)(hash_value & 0xFF));
    
    return std::string(mac);
}

// 检查路径是否存在
bool path_exists(const std::string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

// 解析volume参数 (格式: host_path:container_path)
VolumeInfo parse_volume(const std::string& volume_str) {
    VolumeInfo volume_info = {"", "", false};
    
    size_t colon_pos = volume_str.find(':');
    if (colon_pos != std::string::npos && colon_pos > 0 && colon_pos < volume_str.length() - 1) {
        volume_info.host_path = volume_str.substr(0, colon_pos);
        volume_info.container_path = volume_str.substr(colon_pos + 1);
        volume_info.valid = true;
        std::cout << "[Volume] Parsed volume: " << volume_info.host_path << " -> " << volume_info.container_path << std::endl;
    } else {
        std::cerr << "[Volume] Invalid volume format: " << volume_str << " (expected host_path:container_path)" << std::endl;
    }
    
    return volume_info;
}

// 挂载volume
void mount_volume(const VolumeInfo& volume_info) {
    if (!volume_info.valid) {
        return;
    }
    
    std::cout << "[Volume] Mounting volume..." << std::endl;
    
    // 创建宿主机目录（如果不存在）
    if (!path_exists(volume_info.host_path)) {
        if (mkdir(volume_info.host_path.c_str(), 0777) != 0) {
            perror("mkdir host volume dir failed");
            return;
        }
        std::cout << "[Volume] Created host directory: " << volume_info.host_path << std::endl;
    }
    
    // 创建容器内目录
    std::string container_volume_path = MNT_URL + volume_info.container_path;
    if (mkdir(container_volume_path.c_str(), 0777) != 0) {
        if (errno != EEXIST) {
            perror("mkdir container volume dir failed");
            return;
        }
    }
    
    // 使用bind mount挂载volume
    if (mount(volume_info.host_path.c_str(), container_volume_path.c_str(), "", MS_BIND, nullptr) != 0) {
        perror("mount volume failed");
        return;
    }
    
    std::cout << "[Volume] Volume mounted successfully: " << volume_info.host_path << " -> " << container_volume_path << std::endl;
}

// 卸载volume
void umount_volume(const VolumeInfo& volume_info) {
    if (!volume_info.valid) {
        return;
    }
    
    std::string container_volume_path = MNT_URL + volume_info.container_path;
    std::cout << "[Volume] Unmounting volume: " << container_volume_path << std::endl;
    
    if (umount(container_volume_path.c_str()) != 0) {
        perror("umount volume failed");
    } else {
        std::cout << "[Volume] Volume unmounted successfully" << std::endl;
    }
}

// 创建只读层（解压busybox）
void create_readonly_layer() {
    std::cout << "[FileSystem] Creating readonly layer..." << std::endl;
    
    if (!path_exists(BUSYBOX_URL)) {
        // 创建busybox目录
        if (mkdir(BUSYBOX_URL.c_str(), 0777) != 0) {
            perror("mkdir busybox failed");
            return;
        }
        
        // 解压busybox.tar到busybox目录
        std::string tar_cmd = "sudo tar -xf " + BUSYBOX_TAR_URL + " -C " + BUSYBOX_URL;
        if (system(tar_cmd.c_str()) != 0) {
            std::cerr << "[FileSystem] Failed to extract busybox.tar" << std::endl;
        } else {
            std::cout << "[FileSystem] Busybox extracted successfully" << std::endl;
        }
    } else {
        std::cout << "[FileSystem] Busybox layer already exists" << std::endl;
    }
}

// 创建写入层和工作目录
void create_write_layer() {
    std::cout << "[FileSystem] Creating write layer..." << std::endl;
    
    if (mkdir(WRITE_LAYER_URL.c_str(), 0777) != 0) {
        if (errno != EEXIST) {
            perror("mkdir write layer failed");
        }
    }
    
    // 创建OverlayFS工作目录
    if (mkdir(WORK_DIR_URL.c_str(), 0777) != 0) {
        if (errno != EEXIST) {
            perror("mkdir work dir failed");
        }
    }
}

// 创建OverlayFS挂载点
void create_mount_point() {
    std::cout << "[FileSystem] Creating OverlayFS mount point..." << std::endl;
    
    // 创建挂载目录
    if (mkdir(MNT_URL.c_str(), 0777) != 0) {
        if (errno != EEXIST) {
            perror("mkdir mount point failed");
        }
    }
    
    // 构建OverlayFS挂载命令
    std::string overlay_opts = "lowerdir=" + BUSYBOX_URL + ",upperdir=" + WRITE_LAYER_URL + ",workdir=" + WORK_DIR_URL;
    std::string mount_cmd = "sudo mount -t overlay overlay -o " + overlay_opts + " " + MNT_URL;
    
    if (system(mount_cmd.c_str()) != 0) {
        std::cerr << "[FileSystem] OverlayFS mount failed" << std::endl;
    } else {
        std::cout << "[FileSystem] OverlayFS mounted successfully" << std::endl;
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
    std::string mkdir_cmd = "mkdir -p " + dir_path;
    if (system(mkdir_cmd.c_str()) != 0) {
        std::cerr << "[Container] Failed to create container info directory" << std::endl;
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
    std::string log_file = dir_path + CONTAINER_LOG_FILE;
    std::ofstream log_stream(log_file);
    if (log_stream.is_open()) {
        log_stream.close();
        std::cout << "[Container] Log file created: " << log_file << std::endl;
    } else {
        std::cerr << "[Container] Failed to create log file" << std::endl;
    }
    
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

// 查看容器日志
void show_container_logs(const std::string& container_name) {
    std::cout << "[Container] Showing logs for container: " << container_name << std::endl;
    
    std::string log_file = CONTAINER_INFO_PATH + container_name + "/" + CONTAINER_LOG_FILE;
    
    if (!path_exists(log_file)) {
        std::cerr << "[Container] Log file not found: " << log_file << std::endl;
        return;
    }
    
    std::ifstream log_stream(log_file);
    if (!log_stream.is_open()) {
        std::cerr << "[Container] Failed to open log file: " << log_file << std::endl;
        return;
    }
    
    std::string line;
    while (std::getline(log_stream, line)) {
        std::cout << line << std::endl;
    }
    
    log_stream.close();
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



// 执行系统命令并返回输出
std::string execute_command(const std::string& command) {
    std::string result = "";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        std::cerr << "[Network] Failed to execute command: " << command << std::endl;
        return result;
    }
    
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    return result;
}

// 检查网络接口是否存在
bool interface_exists(const std::string& interface_name) {
    std::string command = "ip link show " + interface_name + " 2>/dev/null";
    std::string output = execute_command(command);
    return !output.empty();
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

// ==================== 网络管理功能 ====================

// 创建目录（如果不存在）
// Todo: make this used in other parts of the code
void create_directory_if_not_exists(const std::string& path) {
    struct stat st = {0};
    if (stat(path.c_str(), &st) == -1) {
        std::string mkdir_cmd = "mkdir -p " + path;
        if (system(mkdir_cmd.c_str()) != 0) {
            std::cerr << "[Network] Failed to create directory: " << path << std::endl;
        }
    }
}


// 创建桥接网络
bool create_bridge_network(const std::string& bridge_name, const std::string& subnet) {
    std::cout << "[Network] Creating bridge network: " << bridge_name << std::endl;
    
    // 检查桥接是否已存在
    if (interface_exists(bridge_name)) {
        std::cout << "[Network] Bridge " << bridge_name << " already exists" << std::endl;
        return true;
    }
    
    // 创建桥接
    std::string create_cmd = "ip link add " + bridge_name + " type bridge";
    if (system(create_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to create bridge: " << bridge_name << std::endl;
        return false;
    }
    
    // 设置桥接IP地址（网关地址）
    std::string gateway_ip = subnet.substr(0, subnet.find_last_of('.')) + ".1/24";
    std::string ip_cmd = "ip addr add " + gateway_ip + " dev " + bridge_name;
    if (system(ip_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to set bridge IP: " << gateway_ip << std::endl;
        return false;
    }
    
    // 启动桥接
    std::string up_cmd = "ip link set " + bridge_name + " up";
    if (system(up_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to bring up bridge: " << bridge_name << std::endl;
        return false;
    }
    
    // 启用IP转发
    std::string enable_forward_cmd = "echo 1 > /proc/sys/net/ipv4/ip_forward";
    system(enable_forward_cmd.c_str());
    
    // 设置iptables规则
    // 1. NAT规则用于出站流量
    std::string iptables_nat_cmd = "iptables -t nat -A POSTROUTING -s " + subnet + " ! -o " + bridge_name + " -j MASQUERADE";
    system(iptables_nat_cmd.c_str());
    
    // 2. FORWARD规则允许容器间和容器到外网的流量
    std::string iptables_forward_in_cmd = "iptables -A FORWARD -i " + bridge_name + " -j ACCEPT";
    system(iptables_forward_in_cmd.c_str());
    
    std::string iptables_forward_out_cmd = "iptables -A FORWARD -o " + bridge_name + " -j ACCEPT";
    system(iptables_forward_out_cmd.c_str());
    
    std::cout << "[Network] Bridge network created successfully" << std::endl;
    return true;
}

// 删除桥接网络
bool delete_bridge_network(const std::string& bridge_name) {
    std::cout << "[Network] Deleting bridge network: " << bridge_name << std::endl;
    
    if (!interface_exists(bridge_name)) {
        std::cout << "[Network] Bridge " << bridge_name << " does not exist" << std::endl;
        return true;
    }
    
    // 删除桥接
    std::string delete_cmd = "ip link delete " + bridge_name;
    if (system(delete_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to delete bridge: " << bridge_name << std::endl;
        return false;
    }
    
    std::cout << "[Network] Bridge network deleted successfully" << std::endl;
    return true;
}

// 保存网络配置到文件
bool save_network_config(const NetworkInfo& network) {
    create_directory_if_not_exists(DEFAULT_NETWORK_PATH);
    
    std::string config_path = DEFAULT_NETWORK_PATH + network.name;
    std::ofstream config_file(config_path);
    if (!config_file.is_open()) {
        std::cerr << "[Network] Failed to create network config file: " << config_path << std::endl;
        return false;
    }
    
    config_file << "{\n";
    config_file << "  \"name\": \"" << network.name << "\",\n";
    config_file << "  \"ip_range\": \"" << network.ip_range << "\",\n";
    config_file << "  \"driver\": \"" << network.driver << "\"\n";
    config_file << "}\n";
    config_file.close();
    
    return true;
}

// 从文件加载网络配置
NetworkInfo load_network_config(const std::string& network_name) {
    NetworkInfo network;
    std::string config_path = DEFAULT_NETWORK_PATH + network_name;
    
    std::ifstream config_file(config_path);
    if (!config_file.is_open()) {
        std::cerr << "[Network] Failed to open network config file: " << config_path << std::endl;
        return network;
    }
    
    std::string line;
    while (std::getline(config_file, line)) {
        if (line.find("\"name\"") != std::string::npos) {
            size_t start = line.find(":") + 1;
            size_t first_quote = line.find("\"", start);
            size_t second_quote = line.find("\"", first_quote + 1);
            network.name = line.substr(first_quote + 1, second_quote - first_quote - 1);
        } else if (line.find("\"ip_range\"") != std::string::npos) {
            size_t start = line.find(":") + 1;
            size_t first_quote = line.find("\"", start);
            size_t second_quote = line.find("\"", first_quote + 1);
            network.ip_range = line.substr(first_quote + 1, second_quote - first_quote - 1);
        } else if (line.find("\"driver\"") != std::string::npos) {
            size_t start = line.find(":") + 1;
            size_t first_quote = line.find("\"", start);
            size_t second_quote = line.find("\"", first_quote + 1);
            network.driver = line.substr(first_quote + 1, second_quote - first_quote - 1);
        }
    }
    config_file.close();
    
    return network;
}

// 删除网络配置文件
bool remove_network_config(const std::string& network_name) {
    std::string config_path = DEFAULT_NETWORK_PATH + network_name;
    if (remove(config_path.c_str()) != 0) {
        std::cerr << "[Network] Failed to remove network config file: " << config_path << std::endl;
        return false;
    }
    return true;
}

// IP分配管理结构
struct IPAMAllocator {
    std::string subnet_file_path;
    std::map<std::string, std::string> subnets; // subnet -> allocation bitmap
    
    IPAMAllocator() : subnet_file_path("/var/run/mydocker/network/ipam/subnet.json") {}
    
    bool load() {
        std::ifstream file(subnet_file_path);
        if (!file.is_open()) {
            return true; // 文件不存在是正常的
        }
        
        std::string line, content;
        while (std::getline(file, line)) {
            content += line;
        }
        file.close();
        
        // 简单的JSON解析（仅支持我们的格式）
        size_t start = 0;
        while ((start = content.find('"', start)) != std::string::npos) {
            size_t key_start = start + 1;
            size_t key_end = content.find('"', key_start);
            if (key_end == std::string::npos) break;
            
            std::string key = content.substr(key_start, key_end - key_start);
            
            start = content.find('"', key_end + 1);
            if (start == std::string::npos) break;
            
            size_t value_start = start + 1;
            size_t value_end = content.find('"', value_start);
            if (value_end == std::string::npos) break;
            
            std::string value = content.substr(value_start, value_end - value_start);
            subnets[key] = value;
            
            start = value_end + 1;
        }
        
        return true;
    }
    
    bool save() {
        // 创建目录
        std::string dir_cmd = "mkdir -p /var/run/mydocker/network/ipam";
        system(dir_cmd.c_str());
        
        std::ofstream file(subnet_file_path);
        if (!file.is_open()) {
            std::cerr << "[IPAM] Failed to open subnet file for writing" << std::endl;
            return false;
        }
        
        file << "{\n";
        bool first = true;
        for (const auto& pair : subnets) {
            if (!first) file << ",\n";
            file << "  \"" << pair.first << "\": \"" << pair.second << "\"";
            first = false;
        }
        file << "\n}\n";
        file.close();
        
        return true;
    }
    
    std::string allocate(const std::string& subnet) {
        load();
        
        // 计算可用IP数量（简化版本，假设/24网络）
        size_t slash_pos = subnet.find('/');
        if (slash_pos == std::string::npos) return "";
        
        int prefix_len = std::stoi(subnet.substr(slash_pos + 1));
        int available_ips = (prefix_len == 24) ? 254 : 30; // /24有254个可用IP，其他默认30个
        
        // 初始化分配位图
        if (subnets.find(subnet) == subnets.end()) {
            subnets[subnet] = std::string(available_ips, '0');
        }
        
        std::string& allocation_map = subnets[subnet];
        
        // 查找第一个可用的IP（从索引1开始，跳过网关IP）
        for (int i = 1; i < available_ips; ++i) {
            if (allocation_map[i] == '0') {
                allocation_map[i] = '1';
                
                // 计算IP地址
                std::string network = subnet.substr(0, slash_pos);
                std::string base_ip = network.substr(0, network.rfind('.'));
                std::string ip = base_ip + "." + std::to_string(i + 1); // +1因为.1是网关
                
                // 保存分配信息
                save();
                
                std::cout << "[IPAM] Allocated IP: " << ip << " for subnet: " << subnet << std::endl;
                return ip;
            }
        }
        
        std::cerr << "[IPAM] No available IP in subnet: " << subnet << std::endl;
        return "";
    }
    
    bool release(const std::string& subnet, const std::string& ip) {
        std::cout << "[IPAM] Releasing IP: " << ip << " from subnet: " << subnet << std::endl;
        
        load();
        
        if (subnets.find(subnet) == subnets.end()) {
            return true; // 子网不存在，认为已释放
        }
        
        // 计算IP在位图中的索引
        std::string network = subnet.substr(0, subnet.find('/'));
        std::string base_ip = network.substr(0, network.rfind('.'));
        
        size_t last_dot = ip.rfind('.');
        if (last_dot == std::string::npos) return false;
        
        int ip_suffix = std::stoi(ip.substr(last_dot + 1));
        int index = ip_suffix - 2; // -2因为索引0对应.2（.1是网关）
        
        if (index >= 0 && index < (int)subnets[subnet].length()) {
            subnets[subnet][index] = '0';
            save();
            return true;
        }
        
        return false;
    }
};

static IPAMAllocator ipam_allocator;

// 改进的IP分配算法
std::string allocate_ip(const std::string& subnet) {
    std::cout << "[Network] Allocating IP for subnet: " << subnet << std::endl;
    return ipam_allocator.allocate(subnet);
}

// 释放IP地址
bool release_ip(const std::string& subnet, const std::string& ip) {
    return ipam_allocator.release(subnet, ip);
}

// 创建veth pair并连接到容器（改进版本，使用IPAM分配IP）
bool setup_container_network(const std::string& container_id, const std::string& network_name, 
                           std::string& container_ip, pid_t container_pid) {
    std::cout << "[Network] Setting up network for container: " << container_id << std::endl;
    
    // 加载网络配置
    NetworkInfo network = load_network_config(network_name);
    if (network.name.empty()) {
        std::cerr << "[Network] Network not found: " << network_name << std::endl;
        return false;
    }
    
    // 如果没有指定IP，则通过IPAM分配
    if (container_ip.empty()) {
        container_ip = ipam_allocator.allocate(network.ip_range);
        if (container_ip.empty()) {
            std::cerr << "[Network] Failed to allocate IP for container in network: " << network_name << std::endl;
            return false;
        }
        std::cout << "[Network] Allocated IP: " << container_ip << " for container: " << container_id << std::endl;
    }
    
    std::string veth_host = "veth" + container_id.substr(0, 5);
    std::string veth_container = "vethpeer0";
    
    // 检查并清理已存在的veth设备
    if (interface_exists(veth_host)) {
        std::cout << "[Network] Cleaning up existing veth interface: " << veth_host << std::endl;
        std::string delete_veth_cmd = "ip link delete " + veth_host;
        system(delete_veth_cmd.c_str());
    }
    
    // 创建veth pair
    std::string create_veth_cmd = "ip link add " + veth_host + " type veth peer name " + veth_container;
    std::cout<<create_veth_cmd<<std::endl;
    if (system(create_veth_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to create veth pair" << std::endl;
        return false;
    }
    
    // 将host端连接到桥接
    std::string attach_bridge_cmd = "ip link set " + veth_host + " master " + network_name;
    if (system(attach_bridge_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to attach veth to bridge" << std::endl;
        return false;
    }
    
    // 启动host端
    std::string up_host_cmd = "ip link set " + veth_host + " up";
    if (system(up_host_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to bring up host veth" << std::endl;
        return false;
    }
    
    // 将container端移动到容器的网络命名空间
    std::string move_to_ns_cmd = "ip link set " + veth_container + " netns " + std::to_string(container_pid);
    if (system(move_to_ns_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to move veth to container namespace" << std::endl;
        return false;
    }
    
    // 在容器命名空间中将veth重命名为eth0
    std::string rename_cmd = "nsenter -t " + std::to_string(container_pid) + " -n ip link set " + veth_container + " name eth0";
    if (system(rename_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to rename container interface to eth0" << std::endl;
        // 清理已创建的veth设备
        std::string cleanup_cmd = "ip link delete " + veth_host;
        system(cleanup_cmd.c_str());
        return false;
    }

    // 生成唯一的MAC地址
    std::string mac_address = generate_unique_mac(container_id);
    std::string set_mac_cmd = "nsenter -t " + std::to_string(container_pid) + " -n ip link set eth0 address " + mac_address;
    if (system(set_mac_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to set MAC address: " << mac_address << std::endl;
        // 清理已创建的veth设备
        std::string cleanup_cmd = "ip link delete " + veth_host;
        system(cleanup_cmd.c_str());
        return false;
    }
    std::cout << "[Network] Set MAC address: " << mac_address << " for container: " << container_id << std::endl;

    // 在容器命名空间中配置网络
    std::string set_ip_cmd = "nsenter -t " + std::to_string(container_pid) + " -n ip addr add " + container_ip + "/24 dev eth0";
    if (system(set_ip_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to set container IP" << std::endl;
        // 清理已创建的veth设备和释放IP
        std::string cleanup_cmd = "ip link delete " + veth_host;
        system(cleanup_cmd.c_str());
        ipam_allocator.release(network.ip_range, container_ip);
        return false;
    }
    
    // 启动容器端网络接口
    std::string up_container_cmd = "nsenter -t " + std::to_string(container_pid) + " -n ip link set eth0 up";
    if (system(up_container_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to bring up container veth" << std::endl;
        // 清理已创建的veth设备和释放IP
        std::string cleanup_cmd = "ip link delete " + veth_host;
        system(cleanup_cmd.c_str());
        ipam_allocator.release(network.ip_range, container_ip);
        return false;
    }
    
    // 启动loopback接口
    std::string up_lo_cmd = "nsenter -t " + std::to_string(container_pid) + " -n ip link set lo up";
    system(up_lo_cmd.c_str());
    
    // 设置默认路由 - 动态计算网关地址
    std::string gateway;
    if (network_name == DEFAULT_BRIDGE_NAME) {
        gateway = "192.168.1.1";
    } else {
        // 从网络配置中获取子网信息并计算网关
        NetworkInfo network = load_network_config(network_name);
        if (!network.ip_range.empty()) {
            size_t slash_pos = network.ip_range.find('/');
            if (slash_pos != std::string::npos) {
                std::string base_ip = network.ip_range.substr(0, slash_pos);
                size_t last_dot = base_ip.find_last_of('.');
                gateway = base_ip.substr(0, last_dot + 1) + "1";
            } else {
                gateway = "192.168.2.1"; // 默认值
            }
        } else {
            gateway = "192.168.2.1"; // 默认值
        }
    }
    
    std::string route_cmd = "nsenter -t " + std::to_string(container_pid) + " -n ip route add default via " + gateway;
    if (system(route_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to set default route via " << gateway << std::endl;
    }
    
    // 设置DNS配置
    std::string dns_cmd = "nsenter -t " + std::to_string(container_pid) + " -m sh -c 'echo \"nameserver 8.8.8.8\" > /etc/resolv.conf'";
    system(dns_cmd.c_str());
    
    std::cout << "[Network] Container network setup completed" << std::endl;
    return true;
}

// 配置端口映射
bool setup_port_mapping(const std::string& container_ip, const std::vector<std::string>& port_mapping) {
    for (const auto& mapping : port_mapping) {
        size_t colon_pos = mapping.find(':');
        if (colon_pos == std::string::npos) {
            std::cerr << "[Network] Invalid port mapping format: " << mapping << std::endl;
            continue;
        }
        
        std::string host_port = mapping.substr(0, colon_pos);
        std::string container_port = mapping.substr(colon_pos + 1);
        
        // 添加iptables规则进行端口转发
        std::string iptables_cmd = "iptables -t nat -A PREROUTING -p tcp --dport " + host_port + 
                                 " -j DNAT --to-destination " + container_ip + ":" + container_port;
        
        if (system(iptables_cmd.c_str()) != 0) {
            std::cerr << "[Network] Failed to setup port mapping: " << mapping << std::endl;
            continue;
        }
        
        std::cout << "[Network] Port mapping setup: " << host_port << " -> " << container_ip << ":" << container_port << std::endl;
    }
    
    return true;
}

// 网络命令处理函数
void network_create(const std::string& driver, const std::string& subnet, const std::string& name) {
    std::cout << "[Network] Creating network: " << name << std::endl;
    
    if (driver != "bridge") {
        std::cerr << "[Network] Only bridge driver is supported" << std::endl;
        return;
    }
    
    // 验证子网格式
    if (subnet.find('/') == std::string::npos) {
        std::cerr << "[Network] Invalid subnet format: " << subnet << std::endl;
        return;
    }
    
    // 检查网络是否已存在
    std::string config_file = DEFAULT_NETWORK_PATH + "/" + name + ".json";
    std::ifstream check_file(config_file);
    if (check_file.is_open()) {
        check_file.close();
        std::cerr << "[Network] Network already exists: " << name << std::endl;
        return;
    }
    
    // 通过IPAM分配网关IP（验证子网可用性）
    std::string gateway_ip = ipam_allocator.allocate(subnet);
    if (gateway_ip.empty()) {
        std::cerr << "[Network] Failed to allocate gateway IP for subnet: " << subnet << std::endl;
        return;
    }
    
    // 创建桥接网络
    if (!create_bridge_network(name, subnet)) {
        std::cerr << "[Network] Failed to create bridge network" << std::endl;
        // 释放已分配的网关IP
        ipam_allocator.release(subnet, gateway_ip);
        return;
    }
    
    // 保存网络配置
    NetworkInfo network;
    network.name = name;
    network.ip_range = subnet;
    network.driver = driver;
    
    if (!save_network_config(network)) {
        std::cerr << "[Network] Failed to save network config" << std::endl;
        // 清理：删除桥接和释放IP
        std::string cleanup_cmd = "ip link delete " + name;
        system(cleanup_cmd.c_str());
        ipam_allocator.release(subnet, gateway_ip);
        return;
    }
    
    std::cout << "[Network] Network created successfully: " << name << " (Gateway: " << gateway_ip << ")" << std::endl;
}

void network_list() {
    std::cout << "[Network] Listing networks:" << std::endl;
    std::cout << "NAME\t\tIP RANGE\t\tDRIVER" << std::endl;
    
    // 读取网络配置目录
    std::string list_cmd = "ls " + DEFAULT_NETWORK_PATH + " 2>/dev/null";
    std::string output = execute_command(list_cmd);
    
    if (output.empty()) {
        std::cout << "No networks found" << std::endl;
        return;
    }
    
    std::istringstream iss(output);
    std::string network_name;
    while (std::getline(iss, network_name)) {
        if (!network_name.empty()) {
            NetworkInfo network = load_network_config(network_name);
            if (!network.name.empty()) {
                std::cout << network.name << "\t\t" << network.ip_range << "\t\t" << network.driver << std::endl;
            }
        }
    }
}

void network_remove(const std::string& name) {
    std::cout << "[Network] Removing network: " << name << std::endl;
    
    // 加载网络配置以获取子网信息
    NetworkInfo network = load_network_config(name);
    if (network.name.empty()) {
        std::cerr << "[Network] Network not found: " << name << std::endl;
        return;
    }
    
    // 删除桥接网络
    if (!delete_bridge_network(name)) {
        std::cerr << "[Network] Failed to delete bridge network" << std::endl;
        return;
    }
    
    // 释放IPAM中的所有IP（简化实现：清空整个子网的分配）
    if (!network.ip_range.empty()) {
        std::cout << "[Network] Releasing IP allocations for subnet: " << network.ip_range << std::endl;
        ipam_allocator.load();
        if (ipam_allocator.subnets.find(network.ip_range) != ipam_allocator.subnets.end()) {
            ipam_allocator.subnets.erase(network.ip_range);
            ipam_allocator.save();
        }
    }
    
    // 删除网络配置
    if (!remove_network_config(name)) {
        std::cerr << "[Network] Failed to remove network config" << std::endl;
        return;
    }
    
    std::cout << "[Network] Network removed successfully: " << name << std::endl;
}

// 创建工作空间（OverlayFS文件系统）
void new_workspace(const VolumeInfo& volume_info = {}) {
    std::cout << "[FileSystem] Setting up container workspace..." << std::endl;
    create_readonly_layer();
    create_write_layer();
    create_mount_point();
    
    // 如果有volume，则挂载volume
    if (volume_info.valid) {
        mount_volume(volume_info);
    }
}

// 删除挂载点
void delete_mount_point() {
    std::cout << "[FileSystem] Cleaning up mount point..." << std::endl;
    
    // 卸载OverlayFS
    std::string umount_cmd = "umount " + MNT_URL;
    if (system(umount_cmd.c_str()) != 0) {
        std::cerr << "[FileSystem] Failed to unmount OverlayFS" << std::endl;
    }
    
    // 删除挂载目录
    if (rmdir(MNT_URL.c_str()) != 0) {
        perror("rmdir mount point failed");
    }
}

// 删除写入层和工作目录
void delete_write_layer() {
    std::cout << "[FileSystem] Cleaning up write layer..." << std::endl;
    
    std::string rm_write_cmd = "rm -rf " + WRITE_LAYER_URL;
    if (system(rm_write_cmd.c_str()) != 0) {
        std::cerr << "[FileSystem] Failed to remove write layer" << std::endl;
    }
    
    std::string rm_work_cmd = "rm -rf " + WORK_DIR_URL;
    if (system(rm_work_cmd.c_str()) != 0) {
        std::cerr << "[FileSystem] Failed to remove work directory" << std::endl;
    }
}

// 删除工作空间
void delete_workspace(const VolumeInfo& volume_info = {}) {
    std::cout << "[FileSystem] Cleaning up workspace..." << std::endl;
    
    // 如果有volume，先卸载volume
    if (volume_info.valid) {
        umount_volume(volume_info);
    }
    
    delete_mount_point();
    delete_write_layer();
}

// pivot_root系统调用包装
int pivot_root(const char* new_root, const char* old_root) {
    return syscall(SYS_pivot_root, new_root, old_root);
}

// 执行pivot_root操作
void setup_pivot_root(const std::string& root) {
    std::cout << "[FileSystem] Setting up pivot_root..." << std::endl;
    
    // 将root重新bind mount到自己，确保新旧root不在同一文件系统
    if (mount(root.c_str(), root.c_str(), nullptr, MS_BIND | MS_REC, nullptr) != 0) {
        perror("bind mount root failed");
        return;
    }
    
    // 创建.pivot_root目录存储old_root
    std::string pivot_dir = root + "/.pivot_root";
    if (mkdir(pivot_dir.c_str(), 0777) != 0) {
        perror("mkdir pivot_root failed");
        return;
    }
    
    // 执行pivot_root
    if (pivot_root(root.c_str(), pivot_dir.c_str()) != 0) {
        perror("pivot_root failed");
        return;
    }
    
    // 切换到新的根目录
    if (chdir("/") != 0) {
        perror("chdir to / failed");
        return;
    }
    
    // 卸载old_root
    if (umount2("/.pivot_root", MNT_DETACH) != 0) {
        perror("unmount old_root failed");
    }
    
    // 删除临时目录
    if (rmdir("/.pivot_root") != 0) {
        perror("remove pivot_root dir failed");
    }
    
    std::cout << "[FileSystem] pivot_root completed successfully" << std::endl;
}

// 设置容器内的文件系统挂载
void setup_mount() {
    
    std::cout << "[FileSystem] Isolating mount propagation..." << std::endl;
    if (mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) != 0) {
        perror("mount(MS_PRIVATE) failed");
    } else {
        std::cout << "[FileSystem] Mount propagation set to private" << std::endl;
    }
    std::cout << "[FileSystem] Setting up container mounts..." << std::endl;
    
    // 使用OverlayFS挂载点作为新的根目录
    std::cout << "[FileSystem] Using mount point: " << MNT_URL << std::endl;
    setup_pivot_root(MNT_URL);
    
    // 切换到容器根目录
    if (chdir("/") != 0) {
        perror("chdir to container root failed");
        return;
    }
    
    // 挂载proc文件系统
    unsigned long mount_flags = MS_NOEXEC | MS_NOSUID | MS_NODEV;
    if (mount("proc", "/proc", "proc", mount_flags, nullptr) != 0) {
        perror("mount proc failed");
    } else {
        std::cout << "[FileSystem] /proc mounted successfully" << std::endl;
    }
    
    // 挂载tmpfs到/dev
    if (mount("tmpfs", "/dev", "tmpfs", MS_NOSUID | MS_STRICTATIME, "mode=755") != 0) {
        perror("mount tmpfs to /dev failed");
    } else {
        std::cout << "[FileSystem] /dev mounted successfully" << std::endl;
    }
        
    // 挂载sysfs到/sys
    if (mount("sysfs", "/sys", "sysfs", MS_NOEXEC | MS_NOSUID | MS_NODEV, nullptr) != 0) {
        perror("mount sysfs to /sys failed");
    } else {
        std::cout << "[FileSystem] /sys mounted successfully" << std::endl;
    }
    
    // 挂载tmpfs到/tmp
    if (mount("tmpfs", "/tmp", "tmpfs", MS_NOEXEC | MS_NOSUID | MS_NODEV, "mode=1777") != 0) {
        perror("mount tmpfs to /tmp failed");
    } else {
        std::cout << "[FileSystem] /tmp mounted successfully" << std::endl;
    }
}

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
        std::cout << "[CGroup] CPU shares set to " << cpu_shares << std::endl;
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
        std::cout << "[CGroup] CPU set to " << cpuset << std::endl;
    }
}

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
        std::cout << "[Container] Redirecting output to log file: " << container_args->log_file_path << std::endl;
        
        // 确保日志文件的父目录存在
        std::string log_dir = container_args->log_file_path.substr(0, container_args->log_file_path.find_last_of('/'));
        std::string mkdir_cmd = "mkdir -p " + log_dir;
        if (system(mkdir_cmd.c_str()) != 0) {
            std::cerr << "[Container] Failed to create log directory: " << log_dir << std::endl;
        }
        
        // 确保日志文件存在
        std::ofstream log_file_check(container_args->log_file_path, std::ios::app);
        if (log_file_check.is_open()) {
            log_file_check.close();
        } else {
            std::cerr << "[Container] Failed to create log file: " << container_args->log_file_path << std::endl;
        }
        
        // 重定向标准输出到日志文件
        if (freopen(container_args->log_file_path.c_str(), "a", stdout) == nullptr) {
            perror("Failed to redirect stdout to log file");
        }
        
        // 重定向标准错误到日志文件
        if (freopen(container_args->log_file_path.c_str(), "a", stderr) == nullptr) {
            perror("Failed to redirect stderr to log file");
        }
        
        // 设置无缓冲，确保日志实时写入
        setbuf(stdout, nullptr);
        setbuf(stderr, nullptr);
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