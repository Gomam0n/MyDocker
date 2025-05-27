# MyDocker

## 容器与虚拟机的区别

### 架构差异
容器和虚拟机是两种不同的虚拟化技术，它们在架构上有本质区别：
- **虚拟机**：通过Hypervisor在宿主机上创建多个完整的虚拟操作系统。每个虚拟机都包含一个完整的操作系统内核、应用程序和必要的二进制文件与库。
- **容器**：共享宿主机的操作系统内核，只打包应用程序及其依赖。容器是操作系统层面的虚拟化，而不是硬件层面的虚拟化。

### 资源效率
- **虚拟机**：资源消耗较大，每个虚拟机都需要分配固定的内存和存储空间，启动时间通常为分钟级。
- **容器**：轻量级，启动迅速（通常为秒级或毫秒级），资源利用率高。多个容器可以共享宿主机的资源，占用空间小。

### 隔离程度
- **虚拟机**：提供完整的隔离环境，安全性较高，适合多租户环境。
- **容器**：隔离性相对较弱，所有容器共享宿主机内核，存在潜在的安全风险。

### 应用场景
- **虚拟机**：适合需要完整操作系统环境的场景，如运行不同操作系统、需要高度隔离的环境等。
- **容器**：适合微服务架构、持续集成/持续部署(CI/CD)、应用打包和分发等场景。

### Docker的优势
Docker作为容器技术的代表，具有以下优势：
- **一致的运行环境**：确保应用在开发、测试和生产环境中的一致性
- **快速部署**：容器可以在几秒内启动和停止
- **版本控制**：支持镜像的版本控制和回滚
- **组件复用**：容器可以作为构建块，方便地组合成复杂应用
- **共享**：Docker Hub提供了大量预构建的镜像，方便分享和使用

## 如何查看机器是ARM还是AMD架构

在容器化应用中，CPU架构是一个重要的考虑因素，因为不同架构需要不同的镜像。以下是在各种操作系统中检查CPU架构的方法：

### Windows系统

1. **使用系统信息命令**：
   ```
   systeminfo | findstr /i "处理器"
   ```
   或英文系统：
   ```
   systeminfo | findstr /i "processor"
   ```

2. **使用PowerShell**：
   ```powershell
   Get-WmiObject -Class Win32_Processor | Select-Object Architecture,Name
   ```
   其中Architecture的值：0代表x86，9代表x64，5和12代表ARM架构。

### Linux系统

1. **使用uname命令**：
   ```bash
   uname -m
   ```
   结果中，`x86_64`表示AMD64/x86_64架构，`aarch64`或`arm64`表示ARM架构。

2. **查看CPU信息**：
   ```bash
   cat /proc/cpuinfo
   ```

### macOS系统

1. **使用uname命令**：
   ```bash
   uname -m
   ```

2. **使用sysctl命令**：
   ```bash
   sysctl -n machdep.cpu.brand_string
   ```

### 在Docker中的重要性

了解CPU架构对Docker使用至关重要，原因如下：

- **镜像兼容性**：ARM架构（如Apple M1/M2芯片、树莓派等）需要专门构建的镜像，无法直接运行为x86/AMD64构建的镜像。
- **性能优化**：针对特定架构优化的镜像可以获得更好的性能。
- **多架构支持**：使用Docker的`buildx`功能可以构建支持多架构的镜像。

在拉取镜像时，可以通过指定架构来获取适合当前系统的镜像：

```bash
# 拉取特定架构的镜像
docker pull --platform=linux/arm64 nginx
# 或
docker pull --platform=linux/amd64 nginx
```


---

## Linux Namespace 机制简介

Linux Namespace 是 Linux 内核提供的一种资源隔离机制。通过 Namespace，内核可以将全局资源（如进程ID、网络、挂载点、用户、主机名等）划分到不同的命名空间中，每个命名空间中的资源彼此隔离。这样，容器中的进程就像运行在独立的系统环境中一样，互不影响。

常见的 Namespace 类型包括：
- **PID Namespace**：隔离进程编号（PID），实现每个容器有独立的进程树。
- **NET Namespace**：隔离网络设备、IP 地址、端口等。
- **MNT Namespace**：隔离挂载点（文件系统视图）。
- **UTS Namespace**：隔离主机名和域名。
- **IPC Namespace**：隔离进程间通信资源。
- **USER Namespace**：隔离用户和用户组ID。

通过这些 Namespace，容器能够实现进程、网络、文件系统等多方面的隔离，是容器技术的核心基础之一。


## cgroups 详解及其在 Docker 中的应用

**cgroups**（Control Groups）是 Linux 内核提供的一种机制，用于限制、记录和隔离进程组所使用的物理资源（如 CPU、内存、磁盘 I/O 等）。它是 Linux 容器技术的核心基础之一。

### cgroups 的主要功能

1. **资源限制（Resource Limiting）**
   - 限制进程组能够使用的资源上限
   - 例如：限制某个进程组最多使用 2GB 内存或 50% CPU

2. **优先级分配（Prioritization）**
   - 通过控制进程组对资源的访问优先级来确保重要任务获得更多资源

3. **资源统计（Accounting）**
   - 统计系统资源的使用情况
   - 可用于计费或监控目的

4. **进程控制（Control）**
   - 对进程组进行挂起、恢复等操作

### cgroups 的层次结构

cgroups 采用层次化的树状结构：
- **cgroup 层次结构**：类似文件系统的目录树
- **子系统（Subsystem）**：具体的资源控制器，如 cpu、memory、blkio 等
- **任务（Task）**：系统中的进程

### 主要的 cgroup 子系统

- **cpu**：限制 CPU 使用率
- **cpuacct**：统计 CPU 使用情况
- **memory**：限制内存使用
- **blkio**：限制块设备 I/O
- **devices**：控制对设备的访问
- **freezer**：挂起或恢复进程组
- **net_cls**：标记网络数据包
- **pid**：限制进程数量

### cgroups 在 Docker 中的应用

Docker 大量使用 cgroups 来实现容器的资源管理和隔离：

#### 1. 资源限制

```bash
# 限制容器内存使用
docker run -m 512m nginx

# 限制 CPU 使用
docker run --cpus="1.5" nginx

# 限制 CPU 份额
docker run --cpu-shares=512 nginx
```

#### 2. 具体应用场景

**内存管理**：
- Docker 通过 memory cgroup 限制容器的内存使用
- 防止某个容器消耗过多内存影响其他容器
- 当容器超出内存限制时，会触发 OOM Killer

**CPU 管理**：
- 使用 cpu cgroup 控制容器的 CPU 使用率
- 支持 CPU 份额分配和绝对限制
- 确保关键服务获得足够的 CPU 资源

**I/O 管理**：
- 通过 blkio cgroup 限制容器的磁盘读写速度
- 防止某个容器的大量 I/O 操作影响系统性能

#### 3. Docker 中的 cgroups 实现

Docker 在 `/sys/fs/cgroup` 目录下为每个容器创建对应的 cgroup：

```bash
# 查看容器的 cgroup 信息
ls /sys/fs/cgroup/memory/docker/
ls /sys/fs/cgroup/cpu/docker/
```

#### 4. 监控和管理

Docker 提供了多种方式来监控容器资源使用：

```bash
# 查看容器资源使用情况
docker stats

# 查看容器详细信息
docker inspect <container_id>
```

#### 5. cgroups v1 vs v2

- **cgroups v1**：传统版本，每个子系统独立的层次结构
- **cgroups v2**：统一层次结构，更简洁的设计，更好的性能

Docker 目前同时支持两个版本，并逐步迁移到 cgroups v2。

### 总结

cgroups 是容器技术的重要基石，它与 namespace 一起构成了 Linux 容器的核心机制：
- **namespace** 提供隔离性（进程看不到彼此）
- **cgroups** 提供资源控制（限制进程能使用多少资源）

在 Docker 中，cgroups 确保了容器之间的资源公平分配和系统稳定性，是实现多租户环境和资源管理的关键技术。


---

### Union File System（联合文件系统）与 AUFS 及其在 Docker 中的应用

#### Union File System 简介
Union File System（联合文件系统，简称 UnionFS）是一种支持将多个不同目录（分支）叠加（mount）到同一个虚拟文件系统中的文件系统。它允许将多个只读层和一个可写层合并为一个统一的文件系统视图，对上层应用透明。

**主要特性：**
- 多层叠加：可以将多个目录（层）合并为一个挂载点，用户看到的是所有层内容的统一视图。
- 只读与可写分离：底层通常为只读层（如基础镜像），最上层为可写层（如容器运行时的更改）。
- 写时复制（Copy-on-Write）：当对只读层的文件进行修改时，文件会被复制到可写层，实际修改只发生在可写层。

#### AUFS 简介
AUFS（Another Union File System）是 Linux 下实现 UnionFS 的一种具体实现。它支持高效的多层叠加、写时复制和分支管理，是 Docker 早期默认采用的存储驱动。

**AUFS 的优势：**
- 支持多层镜像高效叠加，节省存储空间。
- 支持写时复制，保证只读层安全。
- 支持动态添加/删除分支，灵活性高。

#### UnionFS/AUFS 在 Docker 中的应用
Docker 镜像采用分层结构，每一层都是只读的，容器运行时会在镜像层之上添加一个可写层。Docker 通过 UnionFS（如 AUFS、OverlayFS、btrfs 等）将这些层合并为一个统一的文件系统视图。

## OverlayFS 与 AUFS 的区别

- **内核支持**：
  - AUFS 需要额外打补丁或单独编译内核模块，主线 Linux 内核未集成。
  - OverlayFS 从 Linux 3.18 起被主线内核支持，现代发行版默认集成。
- **实现机制**：
  - AUFS 支持多层（multi-branch）挂载，灵活性高，可动态添加/删除分支。
  - OverlayFS 主要支持两层（lowerdir/upperdir），但可通过 lowerdir 逗号分隔实现多层只读。
- **性能与兼容性**：
  - AUFS 功能丰富，兼容性好，但维护压力大，社区活跃度下降。
  - OverlayFS 设计简单，性能优良，社区维护活跃，Docker、Kubernetes 等主流容器平台推荐。
- **写时复制（COW）行为**：
  - 两者都支持写时复制，但 AUFS 支持更复杂的分支合并和白化（whiteout）策略。
  - OverlayFS 的白化机制更简单，行为更易于预测。
- **应用场景**：
  - AUFS 适合需要复杂分层和动态分支管理的场景。
  - OverlayFS 更适合主流容器、轻量级分层文件系统。

> 建议在新项目和容器环境中优先使用 OverlayFS。
- 镜像分层：每次 Dockerfile 的指令（如 RUN、COPY）都会生成一层，所有层通过 UnionFS 叠加。
- 容器写层：容器启动后，所有更改（如新建文件、修改文件）都发生在最上层的可写层，不影响底层镜像。
- 节省空间：多个容器共享相同的只读层，避免重复存储。

**示意图：**
```
容器可写层（Container Layer，可写）
-----------------------------
镜像层N（Image Layer N，只读）
-----------------------------
镜像层N-1（只读）
-----------------------------
基础镜像层（Base Image Layer，只读）
```

**常见 UnionFS 实现：**
- AUFS（早期 Docker 默认，需内核支持）
- OverlayFS（现代 Linux 推荐，Docker 默认）
- btrfs、zfs 等

**注意：**
- AUFS 需要内核补丁，部分发行版默认不支持。
- OverlayFS 现已成为 Docker 的主流存储驱动。

#### 参考命令
- 查看 Docker 当前存储驱动：`docker info | grep Storage` 
- 查看镜像分层：`docker history 镜像名`

---

## proc 文件系统及其在容器中的应用

**proc 文件系统简介：**

proc（process）文件系统是 Linux 内核提供的一个虚拟文件系统，挂载在 `/proc` 目录下。它并不对应实际的磁盘存储，而是内核在内存中动态生成的，主要用于向用户空间暴露内核和进程的实时信息。通过读取 `/proc` 下的各种文件和目录，用户和程序可以获取系统运行状态、进程信息、内存、CPU、网络等详细数据。

**在容器中的作用：**

容器通过 Linux 命名空间（如 PID namespace）实现进程隔离。为了让容器内的进程只能看到自己的进程信息，必须在容器内部重新挂载独立的 proc 文件系统。这样，容器内的 `/proc` 只显示该命名空间下的进程和资源信息，实现与宿主机和其他容器的隔离。

**典型应用场景：**
- 容器初始化时，通常会在新命名空间内执行 `mount -t proc proc /proc` 或通过系统调用 `mount("proc", "/proc", "proc", 0, nullptr)` 挂载 proc 文件系统。
- 容器内的 ps、top、cat /proc/self/status 等命令获取到的都是容器自身视角下的进程和系统信息。

**总结：**
proc 文件系统是容器实现进程隔离和资源可见性隔离的关键机制之一，正确挂载 proc 能保证容器内工具和应用获得独立、准确的系统视图。

