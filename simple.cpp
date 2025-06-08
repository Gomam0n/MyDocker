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

// Volume相关结构
struct VolumeInfo {
    std::string host_path;
    std::string container_path;
    bool valid;
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
        std::cerr << "Usage: " << argv[0] << " <command> [args...] [--mem <MB>] [--cpu <shares>] [--cpuset <cpus>] [-v <host_path:container_path>] [--commit <image_name>] [--name <container_name>] [-d]" << std::endl;
        std::cerr << "       " << argv[0] << " ps" << std::endl;
        std::cerr << "       " << argv[0] << " logs <container_name>" << std::endl;
        std::cerr << "       " << argv[0] << " exec <container_name> <command> [args...]" << std::endl;
        std::cerr << "Example: " << argv[0] << " /bin/sh --mem 100 --cpu 512 --cpuset 0-1 -v /tmp:/tmp --name mycontainer" << std::endl;
        std::cerr << "Detach:  " << argv[0] << " /bin/sh -d --name mycontainer" << std::endl;
        std::cerr << "Commit:  " << argv[0] << " /bin/sh --commit myimage" << std::endl;
        std::cerr << "Exec:    " << argv[0] << " exec mycontainer /bin/ls" << std::endl;
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
    
    std::cout << "[Main] Starting SimpleDocker with filesystem isolation..." << std::endl;
    
    // 默认资源限制
    size_t mem_limit = 50 * 1024 * 1024; // 50MB
    std::string cpu_shares = "";
    std::string cpuset = "";
    std::string volume_str = "";
    std::string commit_image = "";
    std::string container_name = "";
    bool detach_mode = false;
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