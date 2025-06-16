#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <string>
#include "../structures.h"

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

#endif // FILESYSTEM_H