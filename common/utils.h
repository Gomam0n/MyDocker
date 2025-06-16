#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <vector>
#include "structures.h"

// ==================== 基础工具函数 ====================

// 生成随机字符串作为容器ID
std::string generate_container_id(int length = 10);

// 生成唯一的MAC地址
std::string generate_unique_mac(const std::string& container_id);

// 检查路径是否存在
bool path_exists(const std::string& path);

// ==================== Volume管理 ====================

// 解析volume参数 (格式: host_path:container_path)
VolumeInfo parse_volume(const std::string& volume_str);

// 挂载volume
void mount_volume(const VolumeInfo& volume_info);

// 卸载volume
void umount_volume(const VolumeInfo& volume_info);

// ==================== 文件系统管理 ====================

// 创建只读层（解压busybox）
void create_readonly_layer();

// 创建写入层和工作目录
void create_write_layer();

// 创建OverlayFS挂载点
void create_mount_point();

// Commit功能：将容器保存为镜像
void commit_container(const std::string& image_name);


// 创建目录（如果不存在）
void create_directory_if_not_exists(const std::string& path);

#endif // UTILS_H