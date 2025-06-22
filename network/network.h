#ifndef NETWORK_H
#define NETWORK_H

#include <string>
#include <vector>
#include "../common/structures.h"
#include "../common/constants.h"
#include "../common/utils.h"

// 网络接口管理
bool interface_exists(const std::string& interface_name);
std::string execute_command(const std::string& command);

// 桥接网络管理
bool create_bridge_network(const std::string& bridge_name, const std::string& subnet);
bool delete_bridge_network(const std::string& bridge_name);

// 网络配置管理
bool save_network_config(const NetworkInfo& network);
NetworkInfo load_network_config(const std::string& network_name);
bool remove_network_config(const std::string& network_name);

// IP分配管理
std::string allocate_ip(const std::string& subnet);
bool release_ip(const std::string& subnet, const std::string& ip);

// 容器网络设置
bool setup_container_network(const std::string& container_id, const std::string& network_name, 
                           std::string& container_ip, pid_t container_pid);
bool setup_port_mapping(const std::string& container_ip, const std::vector<std::string>& port_mapping);

// 网络命令处理
void network_create(const std::string& driver, const std::string& subnet, const std::string& name);
void network_list();
void network_remove(const std::string& name);

// 全局IPAM分配器
extern IPAMAllocator ipam_allocator;

#endif // NETWORK_H