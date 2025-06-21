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
    struct stat buffer; // as a necessary for stat() only
    return (stat(path.c_str(), &buffer) == 0);
}

// 创建目录（如果不存在）
// Todo: make this used in other parts of the code
void create_directory_if_not_exists(const std::string& path) {
    struct stat buffer;
    if (stat(path.c_str(), &buffer) == -1) {
        std::string mkdir_cmd = "mkdir -p " + path;
        if (system(mkdir_cmd.c_str()) != 0) {
            std::cerr << "[Common] Failed to create directory: " << path << std::endl;
        }
    }
}
