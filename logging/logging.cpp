#include "logging.h"
#include "../common/constants.h"

// 显示容器日志
void show_container_logs(const std::string& container_name) {
    std::cout << "[Container] Showing logs for container: " << container_name << std::endl;
    
    std::string log_file = CONTAINER_INFO_PATH + container_name + "/" + CONTAINER_LOG_FILE;
    
    if (!path_exists(log_file)) {
        std::cerr << "[Container] Log file not found: " << log_file << std::endl;
        return;
    }
    
    std::ifstream log_stream(log_file);
    if (!log_stream.is_open()) {
        std::cerr << "[Container] Failed to open log file: " << log_file << std::endl;
        return;
    }
    
    std::string line;
    while (std::getline(log_stream, line)) {
        std::cout << line << std::endl;
    }
    
    log_stream.close();
}

// 创建容器日志文件
bool create_container_log_file(const std::string& dir_path) {
    std::string log_file = dir_path + CONTAINER_LOG_FILE;
    std::ofstream log_stream(log_file);
    if (log_stream.is_open()) {
        log_stream.close();
        std::cout << "[Container] Log file created: " << log_file << std::endl;
        return true;
    } else {
        std::cerr << "[Container] Failed to create log file" << std::endl;
        return false;
    }
}

// 设置日志重定向（在容器内部使用）
bool setup_log_redirection(const std::string& log_file_path) {
    if (log_file_path.empty()) {
        return false;
    }
    
    std::cout << "[Container] Redirecting output to log file: " << log_file_path << std::endl;
    
    // 确保日志目录存在
    if (!ensure_log_directory(log_file_path)) {
        return false;
    }
    
    // 确保日志文件存在
    std::ofstream log_file_check(log_file_path, std::ios::app);
    if (log_file_check.is_open()) {
        log_file_check.close();
    } else {
        std::cerr << "[Container] Failed to create log file: " << log_file_path << std::endl;
        return false;
    }
    
    // 重定向标准输出到日志文件
    if (freopen(log_file_path.c_str(), "a", stdout) == nullptr) {
        perror("Failed to redirect stdout to log file");
        return false;
    }
    
    // 重定向标准错误到日志文件
    if (freopen(log_file_path.c_str(), "a", stderr) == nullptr) {
        perror("Failed to redirect stderr to log file");
        return false;
    }
    
    // 设置无缓冲，确保日志实时写入
    setbuf(stdout, nullptr);
    setbuf(stderr, nullptr);
    
    return true;
}

// 确保日志目录存在
bool ensure_log_directory(const std::string& log_file_path) {
    std::string log_dir = log_file_path.substr(0, log_file_path.find_last_of('/'));
    std::string mkdir_cmd = "mkdir -p " + log_dir;
    if (system(mkdir_cmd.c_str()) != 0) {
        std::cerr << "[Container] Failed to create log directory: " << log_dir << std::endl;
        return false;
    }
    return true;
}