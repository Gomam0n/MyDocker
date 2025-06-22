#include "filesystem.h"
#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <cstring>
#include "../common/constants.h"
#include "../common/utils.h"

// 创建工作空间（OverlayFS文件系统）
void new_workspace(const VolumeInfo& volume_info) {
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
void delete_workspace(const VolumeInfo& volume_info) {
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

// ==================== Volume管理 ====================

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

// ==================== 文件系统管理 ====================

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