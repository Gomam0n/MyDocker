#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <string>
#include "../common/structures.h"

// OverlayFS工作空间管理
void new_workspace(const VolumeInfo& volume_info = {});
void delete_mount_point();
void delete_write_layer();
void delete_workspace(const VolumeInfo& volume_info = {});

// pivot_root操作
int pivot_root(const char* new_root, const char* old_root);
void setup_pivot_root(const std::string& root);

// 容器内文件系统挂载
void setup_mount();
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

#endif // FILESYSTEM_H