#ifndef CONTAINER_H
#define CONTAINER_H

#include <string>
#include <vector>
#include <sys/types.h>
#include "../common/structures.h"

// 容器信息管理
std::string record_container_info(pid_t container_pid, const std::vector<std::string>& command_array, 
                                  const std::string& container_name, const std::string& container_id);
void delete_container_info(const std::string& container_name);
ContainerInfo parse_container_config(const std::string& config_file);
void list_containers();

// 容器操作
std::string get_container_pid(const std::string& container_name);
std::vector<std::string> get_container_envs(const std::string& container_pid);
void exec_container(const std::string& container_name, const std::vector<std::string>& exec_cmd);
void stop_container(const std::string& container_name);
void remove_container(const std::string& container_name);

// Commit功能：将容器保存为镜像
void commit_container(const std::string& image_name);

#endif // CONTAINER_H