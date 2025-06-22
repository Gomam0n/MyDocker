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

// 创建目录（如果不存在）
bool create_directory_if_not_exists(const std::string& path);

#endif // UTILS_H