## sudo: unable to allocate pty
默认情况下，Linux系统假设你想要重用已挂载的文件系统，这意味着挂载事件会在命名空间之间传播 1 。如果不设置 MS_PRIVATE ，容器内的挂载操作可能会：

- 在宿主机上创建可见的挂载点
- 干扰宿主机的设备文件系统
- 导致设备访问问题（如 sudo: unable to allocate pty 错误）
因此需要将挂载传播隔离
```bash
(mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) != 0
```

## 各种权限问题