#ifndef LOGGING_H
#define LOGGING_H

#include <string>
#include <fstream>
#include <iostream>
#include "common/constants.h"
#include "common/structures.h"
#include "common/utils.h"

// 日志管理相关函数声明

/**
 * 显示容器日志
 * @param container_name 容器名称
 */
void show_container_logs(const std::string& container_name);

/**
 * 创建容器日志文件
 * @param dir_path 容器信息目录路径
 * @return 是否创建成功
 */
bool create_container_log_file(const std::string& dir_path);

/**
 * 设置日志重定向（在容器内部使用）
 * @param log_file_path 日志文件路径
 * @return 是否设置成功
 */
bool setup_log_redirection(const std::string& log_file_path);

/**
 * 确保日志目录存在
 * @param log_file_path 日志文件路径
 * @return 是否创建成功
 */
bool ensure_log_directory(const std::string& log_file_path);

#endif // LOGGING_H