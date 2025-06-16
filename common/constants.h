#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <string>

// Stack size for container processes
#define STACK_SIZE (1024 * 1024)

// cgroup 路径和名称
const std::string CGROUP_ROOT = "/sys/fs/cgroup";
const std::string CGROUP_NAME = "simple_demo";

// 文件系统路径配置
const std::string ROOT_URL = "/home/qianyifan/";
const std::string MNT_URL = "/home/qianyifan/mnt/";
const std::string BUSYBOX_URL = "/home/qianyifan/busybox/";
const std::string BUSYBOX_TAR_URL = "/home/qianyifan/busybox.tar";
const std::string WRITE_LAYER_URL = "/home/qianyifan/writeLayer/";
const std::string WORK_DIR_URL = "/home/qianyifan/work/";

// 容器信息存储路径
const std::string CONTAINER_INFO_PATH = "/var/run/mydocker/";
const std::string CONFIG_NAME = "config.json";
const std::string CONTAINER_LOG_FILE = "container.log";

// 容器状态
const std::string RUNNING = "running";
const std::string STOPPED = "stopped";
const std::string EXITED = "exited";

// 网络相关常量
const std::string DEFAULT_NETWORK_PATH = "/var/run/mydocker/network/network/";
const std::string IPAM_DEFAULT_ALLOCATOR_PATH = "/var/run/mydocker/network/ipam/subnet.json";
const std::string DEFAULT_BRIDGE_NAME = "mydocker0";
const std::string DEFAULT_SUBNET = "192.168.1.0/24";

// 字符集用于生成随机ID
const std::string RANDOM_CHARS = "abcdefghijklmnopqrstuvwxyz0123456789";

#endif // CONSTANTS_H