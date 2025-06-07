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

// Volume相关结构
struct VolumeInfo {
    std::string host_path;
    std::string container_path;
    bool valid;
};

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

// 容器初始化进程，设置文件系统并执行用户命令
int container_init(void* arg) {
    char** argv = static_cast<char**>(arg);
    std::cout << "[Container] Initializing container..." << std::endl;
    std::cout << "[Container] Running command: " << argv[0] << std::endl;
    
    // 设置容器内的文件系统挂载
    setup_mount();
    
    // 执行用户命令
    if (execvp(argv[0], argv) == -1) {
        perror("execvp failed");
        return -1;
    }

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <command> [args...] [--mem <MB>] [--cpu <shares>] [--cpuset <cpus>] [-v <host_path:container_path>] [--commit <image_name>] [-d]" << std::endl;
        std::cerr << "Example: " << argv[0] << " /bin/sh --mem 100 --cpu 512 --cpuset 0-1 -v /tmp:/tmp" << std::endl;
        std::cerr << "Detach:  " << argv[0] << " /bin/sh -d" << std::endl;
        std::cerr << "Commit:  " << argv[0] << " /bin/sh --commit myimage" << std::endl;
        return 1;
    }
    
    std::cout << "[Main] Starting SimpleDocker with filesystem isolation..." << std::endl;
    
    // 默认资源限制
    size_t mem_limit = 50 * 1024 * 1024; // 50MB
    std::string cpu_shares = "";
    std::string cpuset = "";
    std::string volume_str = "";
    std::string commit_image = "";
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
    
    // 创建容器工作空间（OverlayFS文件系统）
    new_workspace(volume_info);
    
    // 创建容器进程
    char* stack = new char[STACK_SIZE];
    char* stackTop = stack + STACK_SIZE;
    
    std::cout << "[Main] Creating container process..." << std::endl;
    int child_pid = clone(container_init, stackTop, 
                         CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS | 
                         CLONE_NEWNET | CLONE_NEWIPC | SIGCHLD, 
                         child_args);
    
    if (child_pid == -1) {
        perror("clone failed");
        delete[] stack;
        delete_workspace();
        return -1;
    }
    
    std::cout << "[Main] Container process created with PID: " << child_pid << std::endl;
    
    // 设置 cgroup 资源限制
    setup_cgroup(child_pid, mem_limit, cpu_shares, cpuset);
    
    if (detach_mode) {
        // Detach模式：不等待容器进程结束，直接返回
        std::cout << "[Main] Container started in detach mode with PID: " << child_pid << std::endl;
        std::cout << "[Main] Container is running in background" << std::endl;
        
        // 在detach模式下不清理资源，让容器继续运行
        delete[] stack;
        std::cout << "[Main] SimpleDocker detached successfully" << std::endl;
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
        
        // 清理资源
        std::cout << "[Main] Cleaning up resources..." << std::endl;
        delete[] stack;
        delete_workspace(volume_info);
        
        std::cout << "[Main] SimpleDocker finished successfully" << std::endl;
        return 0;
    }
}