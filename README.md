# SimpleDocker - A Lightweight Container Runtime

SimpleDocker is a simplified container runtime implementation written in C++ that demonstrates the core concepts of containerization using Linux features.

## Features

### Core Container Features
- **Process Isolation**: Uses Linux namespaces (PID, UTS, Mount, Network, IPC) for complete process isolation
- **Resource Management**: Implements cgroups for CPU, memory, and cpuset resource limiting
- **Filesystem Isolation**: Uses OverlayFS for efficient layered filesystem management
- **Volume Mounting**: Supports host-to-container volume mapping
- **Environment Variables**: Custom environment variable support

### Container Management
- **Container Lifecycle**: Create, start, stop, remove containers
- **Background Execution**: Detached mode support with logging
- **Container Listing**: View running and stopped containers
- **Log Management**: Container log viewing and management
- **Container Execution**: Execute commands in running containers
- **Image Commit**: Save container state as reusable images

### Network Management
- **Bridge Networks**: Create and manage custom bridge networks
- **IP Allocation**: Automatic IP address management (IPAM)
- **Port Mapping**: Host-to-container port forwarding
- **Network Isolation**: Per-container network namespaces

## Architecture

The project is organized into modular components:

- **`container/`**: Container lifecycle management and operations
- **`filesystem/`**: OverlayFS and volume management
- **`network/`**: Network configuration and management
- **`cgroup/`**: Resource limitation and control
- **`logging/`**: Container logging and output redirection
- **`common/`**: Shared utilities, constants, and data structures

## Prerequisites

- Linux operating system (Ubuntu 18.04+ recommended)
- GCC with C++17 support
- CMake 3.10+
- Root privileges (for namespace and cgroup operations)
- BusyBox filesystem (Optional) (for container base image)

## Building

```bash
# Clone the repository
git clone <repository-url>
cd MyDocker

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
make

# The executable will be created as 'simple' or you may change the name as you want in CMakeLists.txt
```

**Note**: Before building, you need to modify the path constants in `common/constants.h` to match your system configuration, particularly the filesystem paths for container storage and BusyBox root directory.

## Usage

### Basic Container Operations

#### Run a Container
```bash
# Basic container execution
./simple /bin/sh

# With resource limits
./simple /bin/sh --mem 100 --cpu 512 --cpuset 0-1

# With volume mounting
./simple /bin/sh -v /tmp:/tmp

# With environment variables
./simple /bin/sh -e MY_VAR=hello -e PATH=/usr/bin

# Named container in detached mode
./simple /bin/sh -d --name mycontainer
```

#### Container Management
```bash
# List all containers
./simple ps

# View container logs
./simple logs mycontainer

# Execute command in running container
./simple exec mycontainer /bin/ls

# Stop a running container
./simple stop mycontainer

# Remove a container
./simple rm mycontainer
```

#### Image Management
```bash
# Commit container to image
./simple /bin/sh --commit myimage
```

### Network Management

#### Network Operations
```bash
# Create a custom network
./simple network create --driver bridge --subnet 192.168.2.0/24 testnet

# List networks
./simple network list

# Remove a network
./simple network remove testnet
```

#### Container with Custom Network
```bash
# Run container with custom network and port mapping
./simple /bin/sh --net testnet -p 8080:80 --name webserver
```

### Advanced Usage

#### Complete Example
```bash
# Run a web server container with full configuration
./simple /bin/sh \
  --name webserver \
  --mem 256 \
  --cpu 1024 \
  --cpuset 0-1 \
  -v /var/www:/var/www \
  -e SERVER_PORT=80 \
  --net custom-network \
  -p 8080:80 \
  -d
```

## Command Line Options

| Option | Description | Example |
|--------|-------------|----------|
| `--mem <MB>` | Memory limit in MB | `--mem 256` |
| `--cpu <shares>` | CPU shares (relative weight) | `--cpu 512` |
| `--cpuset <cpus>` | CPU cores to use | `--cpuset 0-1` |
| `-v <host:container>` | Volume mapping | `-v /tmp:/tmp` |
| `-e <key=value>` | Environment variable | `-e PATH=/usr/bin` |
| `--net <network>` | Network name | `--net mynetwork` |
| `-p <host:container>` | Port mapping | `-p 8080:80` |
| `--name <name>` | Container name | `--name mycontainer` |
| `-d` | Detached mode | `-d` |
| `--commit <image>` | Commit to image | `--commit myimage` |


## Technical Details

### Linux Namespaces Used
- **PID Namespace**: Process isolation
- **UTS Namespace**: Hostname isolation
- **Mount Namespace**: Filesystem isolation
- **Network Namespace**: Network isolation
- **IPC Namespace**: Inter-process communication isolation

### Cgroups Integration
- **Memory**: `memory.limit_in_bytes`
- **CPU**: `cpu.shares`
- **CPUSet**: `cpuset.cpus`

### Filesystem Technology
- **OverlayFS**: Layered filesystem with lower, upper, and work directories
- **Pivot Root**: Root filesystem switching for container isolation

## Limitations

- Requires root privileges for most operations
- Linux-specific implementation
- Basic security model (not production-ready)
