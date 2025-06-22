#include "network.h"
#include "common/constants.h"
#include "common/structures.h"
#include "common/utils.h"
#include <iostream>
#include <fstream>
#include <sstream>

// 全局IPAM分配器
IPAMAllocator ipam_allocator;

// 执行系统命令并返回输出
std::string execute_command(const std::string& command) {
    std::string result = "";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        std::cerr << "[Network] Failed to execute command: " << command << std::endl;
        return result;
    }
    
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    return result;
}

// 检查网络接口是否存在
bool interface_exists(const std::string& interface_name) {
    std::string command = "ip link show " + interface_name + " 2>/dev/null";
    std::string output = execute_command(command);
    return !output.empty();
}

// 创建桥接网络
bool create_bridge_network(const std::string& bridge_name, const std::string& subnet) {
    std::cout << "[Network] Creating bridge network: " << bridge_name << std::endl;
    
    // 检查桥接是否已存在
    if (interface_exists(bridge_name)) {
        std::cout << "[Network] Bridge " << bridge_name << " already exists" << std::endl;
        return true;
    }
    
    // 创建桥接
    std::string create_cmd = "ip link add " + bridge_name + " type bridge";
    if (system(create_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to create bridge: " << bridge_name << std::endl;
        return false;
    }
    
    // 设置桥接IP地址（网关地址）
    std::string gateway_ip = subnet.substr(0, subnet.find_last_of('.')) + ".1/24";
    std::string ip_cmd = "ip addr add " + gateway_ip + " dev " + bridge_name;
    if (system(ip_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to set bridge IP: " << gateway_ip << std::endl;
        return false;
    }
    
    // 启动桥接
    std::string up_cmd = "ip link set " + bridge_name + " up";
    if (system(up_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to bring up bridge: " << bridge_name << std::endl;
        return false;
    }
    
    // 启用IP转发
    std::string enable_forward_cmd = "echo 1 > /proc/sys/net/ipv4/ip_forward";
    system(enable_forward_cmd.c_str());
    
    // 设置iptables规则
    // 1. NAT规则用于出站流量
    std::string iptables_nat_cmd = "iptables -t nat -A POSTROUTING -s " + subnet + " ! -o " + bridge_name + " -j MASQUERADE";
    system(iptables_nat_cmd.c_str());
    
    // 2. FORWARD规则允许容器间和容器到外网的流量
    std::string iptables_forward_in_cmd = "iptables -A FORWARD -i " + bridge_name + " -j ACCEPT";
    system(iptables_forward_in_cmd.c_str());
    
    std::string iptables_forward_out_cmd = "iptables -A FORWARD -o " + bridge_name + " -j ACCEPT";
    system(iptables_forward_out_cmd.c_str());
    
    std::cout << "[Network] Bridge network created successfully" << std::endl;
    return true;
}

// 删除桥接网络
bool delete_bridge_network(const std::string& bridge_name) {
    std::cout << "[Network] Deleting bridge network: " << bridge_name << std::endl;
    
    if (!interface_exists(bridge_name)) {
        std::cout << "[Network] Bridge " << bridge_name << " does not exist" << std::endl;
        return true;
    }
    
    // 删除桥接
    std::string delete_cmd = "ip link delete " + bridge_name;
    if (system(delete_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to delete bridge: " << bridge_name << std::endl;
        return false;
    }
    
    std::cout << "[Network] Bridge network deleted successfully" << std::endl;
    return true;
}

// 保存网络配置到文件
bool save_network_config(const NetworkInfo& network) {
    create_directory_if_not_exists(DEFAULT_NETWORK_PATH);
    
    std::string config_path = DEFAULT_NETWORK_PATH + network.name;
    std::ofstream config_file(config_path);
    if (!config_file.is_open()) {
        std::cerr << "[Network] Failed to create network config file: " << config_path << std::endl;
        return false;
    }
    
    config_file << "{\n";
    config_file << "  \"name\": \"" << network.name << "\",\n";
    config_file << "  \"ip_range\": \"" << network.ip_range << "\",\n";
    config_file << "  \"driver\": \"" << network.driver << "\"\n";
    config_file << "}\n";
    config_file.close();
    
    return true;
}

// 从文件加载网络配置
NetworkInfo load_network_config(const std::string& network_name) {
    NetworkInfo network;
    std::string config_path = DEFAULT_NETWORK_PATH + network_name;
    
    std::ifstream config_file(config_path);
    if (!config_file.is_open()) {
        std::cerr << "[Network] Failed to open network config file: " << config_path << std::endl;
        return network;
    }
    
    std::string line;
    while (std::getline(config_file, line)) {
        if (line.find("\"name\"") != std::string::npos) {
            size_t start = line.find(":") + 1;
            size_t first_quote = line.find("\"", start);
            size_t second_quote = line.find("\"", first_quote + 1);
            network.name = line.substr(first_quote + 1, second_quote - first_quote - 1);
        } else if (line.find("\"ip_range\"") != std::string::npos) {
            size_t start = line.find(":") + 1;
            size_t first_quote = line.find("\"", start);
            size_t second_quote = line.find("\"", first_quote + 1);
            network.ip_range = line.substr(first_quote + 1, second_quote - first_quote - 1);
        } else if (line.find("\"driver\"") != std::string::npos) {
            size_t start = line.find(":") + 1;
            size_t first_quote = line.find("\"", start);
            size_t second_quote = line.find("\"", first_quote + 1);
            network.driver = line.substr(first_quote + 1, second_quote - first_quote - 1);
        }
    }
    config_file.close();
    
    return network;
}

// 删除网络配置文件
bool remove_network_config(const std::string& network_name) {
    std::string config_path = DEFAULT_NETWORK_PATH + network_name;
    if (remove(config_path.c_str()) != 0) {
        std::cerr << "[Network] Failed to remove network config file: " << config_path << std::endl;
        return false;
    }
    return true;
}

// IP分配管理结构实现
IPAMAllocator::IPAMAllocator() : subnet_file_path("/var/run/mydocker/network/ipam/subnet.json") {}

bool IPAMAllocator::load() {
    std::ifstream file(subnet_file_path);
    if (!file.is_open()) {
        return true; // 文件不存在是正常的
    }
    
    std::string line, content;
    while (std::getline(file, line)) {
        content += line;
    }
    file.close();
    
    // 简单的JSON解析（仅支持我们的格式）
    size_t start = 0;
    while ((start = content.find('"', start)) != std::string::npos) {
        size_t key_start = start + 1;
        size_t key_end = content.find('"', key_start);
        if (key_end == std::string::npos) break;
        
        std::string key = content.substr(key_start, key_end - key_start);
        
        start = content.find('"', key_end + 1);
        if (start == std::string::npos) break;
        
        size_t value_start = start + 1;
        size_t value_end = content.find('"', value_start);
        if (value_end == std::string::npos) break;
        
        std::string value = content.substr(value_start, value_end - value_start);
        subnets[key] = value;
        
        start = value_end + 1;
    }
    
    return true;
}

bool IPAMAllocator::save() {
    // 创建目录
    std::string dir_cmd = "mkdir -p /var/run/mydocker/network/ipam";
    system(dir_cmd.c_str());
    
    std::ofstream file(subnet_file_path);
    if (!file.is_open()) {
        std::cerr << "[IPAM] Failed to open subnet file for writing" << std::endl;
        return false;
    }
    
    file << "{\n";
    bool first = true;
    for (const auto& pair : subnets) {
        if (!first) file << ",\n";
        file << "  \"" << pair.first << "\": \"" << pair.second << "\"";
        first = false;
    }
    file << "\n}\n";
    file.close();
    
    return true;
}

std::string IPAMAllocator::allocate(const std::string& subnet) {
    load();
    
    // 计算可用IP数量（简化版本，假设/24网络）
    size_t slash_pos = subnet.find('/');
    if (slash_pos == std::string::npos) return "";
    
    int prefix_len = std::stoi(subnet.substr(slash_pos + 1));
    int available_ips = (prefix_len == 24) ? 254 : 30; // /24有254个可用IP，其他默认30个
    
    // 初始化分配位图
    if (subnets.find(subnet) == subnets.end()) {
        subnets[subnet] = std::string(available_ips, '0');
    }
    
    std::string& allocation_map = subnets[subnet];
    
    // 查找第一个可用的IP（从索引1开始，跳过网关IP）
    for (int i = 1; i < available_ips; ++i) {
        if (allocation_map[i] == '0') {
            allocation_map[i] = '1';
            
            // 计算IP地址
            std::string network = subnet.substr(0, slash_pos);
            std::string base_ip = network.substr(0, network.rfind('.'));
            std::string ip = base_ip + "." + std::to_string(i + 1); // +1因为.1是网关
            
            // 保存分配信息
            save();
            
            std::cout << "[IPAM] Allocated IP: " << ip << " for subnet: " << subnet << std::endl;
            return ip;
        }
    }
    
    std::cerr << "[IPAM] No available IP in subnet: " << subnet << std::endl;
    return "";
}

bool IPAMAllocator::release(const std::string& subnet, const std::string& ip) {
    std::cout << "[IPAM] Releasing IP: " << ip << " from subnet: " << subnet << std::endl;
    
    load();
    
    if (subnets.find(subnet) == subnets.end()) {
        return true; // 子网不存在，认为已释放
    }
    
    // 计算IP在位图中的索引
    std::string network = subnet.substr(0, subnet.find('/'));
    std::string base_ip = network.substr(0, network.rfind('.'));
    
    size_t last_dot = ip.rfind('.');
    if (last_dot == std::string::npos) return false;
    
    int ip_suffix = std::stoi(ip.substr(last_dot + 1));
    int index = ip_suffix - 2; // -2因为索引0对应.2（.1是网关）
    
    if (index >= 0 && index < (int)subnets[subnet].length()) {
        subnets[subnet][index] = '0';
        save();
        return true;
    }
    
    return false;
}

// 改进的IP分配算法
std::string allocate_ip(const std::string& subnet) {
    std::cout << "[Network] Allocating IP for subnet: " << subnet << std::endl;
    return ipam_allocator.allocate(subnet);
}

// 释放IP地址
bool release_ip(const std::string& subnet, const std::string& ip) {
    return ipam_allocator.release(subnet, ip);
}

// 创建veth pair并连接到容器（改进版本，使用IPAM分配IP）
bool setup_container_network(const std::string& container_id, const std::string& network_name, 
                           std::string& container_ip, pid_t container_pid) {
    std::cout << "[Network] Setting up network for container: " << container_id << std::endl;
    
    // 加载网络配置
    NetworkInfo network = load_network_config(network_name);
    if (network.name.empty()) {
        std::cerr << "[Network] Network not found: " << network_name << std::endl;
        return false;
    }
    
    // 如果没有指定IP，则通过IPAM分配
    if (container_ip.empty()) {
        container_ip = ipam_allocator.allocate(network.ip_range);
        if (container_ip.empty()) {
            std::cerr << "[Network] Failed to allocate IP for container in network: " << network_name << std::endl;
            return false;
        }
        std::cout << "[Network] Allocated IP: " << container_ip << " for container: " << container_id << std::endl;
    }
    // Todo: 检查IP是否已分配
    
    std::string veth_host = "veth" + container_id.substr(0, 5);
    std::string veth_container = "vethpeer0";
    
    // 检查并清理已存在的veth设备
    if (interface_exists(veth_host)) {
        std::cout << "[Network] Cleaning up existing veth interface: " << veth_host << std::endl;
        std::string delete_veth_cmd = "ip link delete " + veth_host;
        system(delete_veth_cmd.c_str());
    }
    
    // 创建veth pair
    std::string create_veth_cmd = "ip link add " + veth_host + " type veth peer name " + veth_container;
    std::cout<<create_veth_cmd<<std::endl;
    if (system(create_veth_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to create veth pair" << std::endl;
        return false;
    }
    
    // 将host端连接到桥接
    std::string attach_bridge_cmd = "ip link set " + veth_host + " master " + network_name;
    if (system(attach_bridge_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to attach veth to bridge" << std::endl;
        return false;
    }
    
    // 启动host端
    std::string up_host_cmd = "ip link set " + veth_host + " up";
    if (system(up_host_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to bring up host veth" << std::endl;
        return false;
    }
    
    // 将container端移动到容器的网络命名空间
    std::string move_to_ns_cmd = "ip link set " + veth_container + " netns " + std::to_string(container_pid);
    if (system(move_to_ns_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to move veth to container namespace" << std::endl;
        return false;
    }
    
    // 在容器命名空间中将veth重命名为eth0
    std::string rename_cmd = "nsenter -t " + std::to_string(container_pid) + " -n ip link set " + veth_container + " name eth0";
    if (system(rename_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to rename container interface to eth0" << std::endl;
        // 清理已创建的veth设备
        std::string cleanup_cmd = "ip link delete " + veth_host;
        system(cleanup_cmd.c_str());
        return false;
    }

    // 生成唯一的MAC地址
    std::string mac_address = generate_unique_mac(container_id);
    std::string set_mac_cmd = "nsenter -t " + std::to_string(container_pid) + " -n ip link set eth0 address " + mac_address;
    if (system(set_mac_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to set MAC address: " << mac_address << std::endl;
        // 清理已创建的veth设备
        std::string cleanup_cmd = "ip link delete " + veth_host;
        system(cleanup_cmd.c_str());
        return false;
    }
    std::cout << "[Network] Set MAC address: " << mac_address << " for container: " << container_id << std::endl;

    // 在容器命名空间中配置网络
    std::string set_ip_cmd = "nsenter -t " + std::to_string(container_pid) + " -n ip addr add " + container_ip + "/24 dev eth0";
    if (system(set_ip_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to set container IP" << std::endl;
        // 清理已创建的veth设备和释放IP
        std::string cleanup_cmd = "ip link delete " + veth_host;
        system(cleanup_cmd.c_str());
        ipam_allocator.release(network.ip_range, container_ip);
        return false;
    }
    
    // 启动容器端网络接口
    std::string up_container_cmd = "nsenter -t " + std::to_string(container_pid) + " -n ip link set eth0 up";
    if (system(up_container_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to bring up container veth" << std::endl;
        // 清理已创建的veth设备和释放IP
        std::string cleanup_cmd = "ip link delete " + veth_host;
        system(cleanup_cmd.c_str());
        ipam_allocator.release(network.ip_range, container_ip);
        return false;
    }
    
    // 启动loopback接口
    std::string up_lo_cmd = "nsenter -t " + std::to_string(container_pid) + " -n ip link set lo up";
    system(up_lo_cmd.c_str());
    
    // 设置默认路由 - 动态计算网关地址
    std::string gateway;
    if (network_name == DEFAULT_BRIDGE_NAME) {
        gateway = "192.168.1.1";
    } else {
        // 从网络配置中获取子网信息并计算网关
        NetworkInfo network = load_network_config(network_name);
        if (!network.ip_range.empty()) {
            size_t slash_pos = network.ip_range.find('/');
            if (slash_pos != std::string::npos) {
                std::string base_ip = network.ip_range.substr(0, slash_pos);
                size_t last_dot = base_ip.find_last_of('.');
                gateway = base_ip.substr(0, last_dot + 1) + "1";
            } else {
                gateway = "192.168.2.1"; // 默认值
            }
        } else {
            gateway = "192.168.2.1"; // 默认值
        }
    }
    
    std::string route_cmd = "nsenter -t " + std::to_string(container_pid) + " -n ip route add default via " + gateway;
    if (system(route_cmd.c_str()) != 0) {
        std::cerr << "[Network] Failed to set default route via " << gateway << std::endl;
    }
    
    // 设置DNS配置
    std::string dns_cmd = "nsenter -t " + std::to_string(container_pid) + " -m sh -c 'echo \"nameserver 8.8.8.8\" > /etc/resolv.conf'";
    system(dns_cmd.c_str());
    
    std::cout << "[Network] Container network setup completed" << std::endl;
    return true;
}

// 配置端口映射
bool setup_port_mapping(const std::string& container_ip, const std::vector<std::string>& port_mapping) {
    for (const auto& mapping : port_mapping) {
        size_t colon_pos = mapping.find(':');
        if (colon_pos == std::string::npos) {
            std::cerr << "[Network] Invalid port mapping format: " << mapping << std::endl;
            continue;
        }
        
        std::string host_port = mapping.substr(0, colon_pos);
        std::string container_port = mapping.substr(colon_pos + 1);
        
        // 添加iptables规则进行端口转发
        std::string iptables_cmd = "iptables -t nat -A PREROUTING -p tcp --dport " + host_port + 
                                 " -j DNAT --to-destination " + container_ip + ":" + container_port;
        
        if (system(iptables_cmd.c_str()) != 0) {
            std::cerr << "[Network] Failed to setup port mapping: " << mapping << std::endl;
            continue;
        }
        
        std::cout << "[Network] Port mapping setup: " << host_port << " -> " << container_ip << ":" << container_port << std::endl;
    }
    
    return true;
}

// 网络命令处理函数
void network_create(const std::string& driver, const std::string& subnet, const std::string& name) {
    std::cout << "[Network] Creating network: " << name << std::endl;
    
    if (driver != "bridge") {
        std::cerr << "[Network] Only bridge driver is supported" << std::endl;
        return;
    }
    
    // 验证子网格式
    if (subnet.find('/') == std::string::npos) {
        std::cerr << "[Network] Invalid subnet format: " << subnet << std::endl;
        return;
    }
    
    // 检查网络是否已存在
    std::string config_file = DEFAULT_NETWORK_PATH + "/" + name + ".json";
    std::ifstream check_file(config_file);
    if (check_file.is_open()) {
        check_file.close();
        std::cerr << "[Network] Network already exists: " << name << std::endl;
        return;
    }
    
    // 通过IPAM分配网关IP（验证子网可用性）
    std::string gateway_ip = ipam_allocator.allocate(subnet);
    if (gateway_ip.empty()) {
        std::cerr << "[Network] Failed to allocate gateway IP for subnet: " << subnet << std::endl;
        return;
    }
    
    // 创建桥接网络
    if (!create_bridge_network(name, subnet)) {
        std::cerr << "[Network] Failed to create bridge network" << std::endl;
        // 释放已分配的网关IP
        ipam_allocator.release(subnet, gateway_ip);
        return;
    }
    
    // 保存网络配置
    NetworkInfo network;
    network.name = name;
    network.ip_range = subnet;
    network.driver = driver;
    
    if (!save_network_config(network)) {
        std::cerr << "[Network] Failed to save network config" << std::endl;
        // 清理：删除桥接和释放IP
        std::string cleanup_cmd = "ip link delete " + name;
        system(cleanup_cmd.c_str());
        ipam_allocator.release(subnet, gateway_ip);
        return;
    }
    
    std::cout << "[Network] Network created successfully: " << name << " (Gateway: " << gateway_ip << ")" << std::endl;
}

void network_list() {
    std::cout << "[Network] Listing networks:" << std::endl;
    std::cout << "NAME\t\tIP RANGE\t\tDRIVER" << std::endl;
    
    // 读取网络配置目录
    std::string list_cmd = "ls " + DEFAULT_NETWORK_PATH + " 2>/dev/null";
    std::string output = execute_command(list_cmd);
    
    if (output.empty()) {
        std::cout << "No networks found" << std::endl;
        return;
    }
    
    std::istringstream iss(output);
    std::string network_name;
    while (std::getline(iss, network_name)) {
        if (!network_name.empty()) {
            NetworkInfo network = load_network_config(network_name);
            if (!network.name.empty()) {
                std::cout << network.name << "\t\t" << network.ip_range << "\t\t" << network.driver << std::endl;
            }
        }
    }
}

void network_remove(const std::string& name) {
    std::cout << "[Network] Removing network: " << name << std::endl;
    
    // 加载网络配置以获取子网信息
    NetworkInfo network = load_network_config(name);
    if (network.name.empty()) {
        std::cerr << "[Network] Network not found: " << name << std::endl;
        return;
    }
    
    // 删除桥接网络
    if (!delete_bridge_network(name)) {
        std::cerr << "[Network] Failed to delete bridge network" << std::endl;
        return;
    }
    
    // 释放IPAM中的所有IP（简化实现：清空整个子网的分配）
    if (!network.ip_range.empty()) {
        std::cout << "[Network] Releasing IP allocations for subnet: " << network.ip_range << std::endl;
        ipam_allocator.load();
        if (ipam_allocator.subnets.find(network.ip_range) != ipam_allocator.subnets.end()) {
            ipam_allocator.subnets.erase(network.ip_range);
            ipam_allocator.save();
        }
    }
    
    // 删除网络配置
    if (!remove_network_config(name)) {
        std::cerr << "[Network] Failed to remove network config" << std::endl;
        return;
    }
    
    std::cout << "[Network] Network removed successfully: " << name << std::endl;
}