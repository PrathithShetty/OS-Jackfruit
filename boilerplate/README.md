# Multi-Container Runtime (OS Project)

## Overview
This project implements a lightweight container runtime in C with a supervisor process and a kernel-space memory monitor.

## Features
- Multi-container execution using clone()
- Filesystem isolation using chroot()
- Namespace isolation (PID, UTS, mount)
- Supervisor with IPC (Unix sockets)
- Container metadata tracking (ps)
- Logging system (per-container logs)
- Kernel module for memory monitoring
- Soft and hard memory limits enforcement

## How to Run

### Build
make clean
make

### Load Kernel Module
sudo insmod monitor.ko

### Start Supervisor
sudo ./engine supervisor ../rootfs-base

### Run Container
sudo ./engine start c1 ../rootfs-alpha /bin/ls

### Commands
sudo ./engine ps
sudo ./engine logs c1
sudo ./engine stop c1

### Memory Monitoring
sudo ./engine start mem ../rootfs-alpha ./memory_hog
sudo dmesg | tail

## Design Decisions

- clone() used for namespace isolation
- chroot() used for filesystem isolation
- Unix domain sockets used for IPC
- Linked list used for container metadata
- Kernel timer used for periodic memory monitoring

## Scheduling Observations

CPU-bound processes compete fairly under default scheduler.
Processes with higher nice value get lower priority.

## Limitations

- No full cgroup integration
- Logging is simple file-based
- No network namespace

## Conclusion

This project demonstrates core container concepts including isolation, process control, and kernel-user interaction.
