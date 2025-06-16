#include "utils.h"
#include "constants.h"
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <sys/stat.h>
#include <sys/mount.h>
#include <unistd.h>
#include <errno.h>
#include <cstdio>

// ==================== 基础工具函数 ====================

// 生成随机字符串作为容器ID
std::string generate_container_id(int length) {
    std::string result;
    srand(time(nullptr));
    for (int i = 0; i < length; ++i) {
        result += RANDOM_CHARS[rand() % RANDOM_CHARS.length()];
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
