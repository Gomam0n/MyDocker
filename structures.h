#ifndef STRUCTURES_H
#define STRUCTURES_H

#include <string>
#include <vector>

// Volume相关结构
struct VolumeInfo {
    std::string host_path;
    std::string container_path;
    bool valid;
};

// 网络相关结构
struct NetworkInfo {
    std::string name;
    std::string ip_range;
    std::string driver;
};

struct EndpointInfo {
    std::string id;
    std::string ip_address;
    std::string mac_address;
    std::string network_name;
    std::vector<std::string> port_mapping;
};

struct IPAMInfo {
    std::string subnet;
    std::string allocation_map;
};

// 容器信息结构
struct ContainerInfo {
    std::string id;
    std::string name;
    std::string pid;
    std::string command;
    std::string created_time;
    std::string status;
};

#endif // STRUCTURES_H