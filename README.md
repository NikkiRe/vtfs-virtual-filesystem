# VTFS - Virtual File System

A Linux kernel module implementing a virtual file system with dual-mode operation: **RAM mode** for in-memory storage and **Server mode** for persistent remote storage via HTTP API.

## 📋 Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Features](#features)
- [Components](#components)
- [Requirements](#requirements)
- [Building](#building)
- [Installation](#installation)
- [Usage](#usage)
- [API Reference](#api-reference)
- [Testing](#testing)
- [Project Structure](#project-structure)
- [License](#license)

## 🎯 Overview

VTFS is a custom Linux file system that provides two operational modes:

- **RAM Mode**: Files are stored entirely in kernel memory. Fast but volatile - data is lost on unmount.
- **Server Mode**: Files are persisted remotely on a Spring Boot server with PostgreSQL backend. Data survives unmounting and system reboots.

The file system supports standard POSIX operations including file/directory creation, reading, writing, deletion, hard links, and directory traversal.

## 🏗️ Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Linux Kernel Space                      │
│  ┌──────────────────────────────────────────────────────┐  │
│  │          VTFS Kernel Module (vtfs.ko)                 │  │
│  │  ┌──────────────┐         ┌──────────────────────┐   │  │
│  │  │  File System │         │   HTTP Client Layer  │   │  │
│  │  │  Operations  │◄───────►│   (http.c/http.h)   │   │  │
│  │  └──────────────┘         └──────────┬───────────┘   │  │
│  └───────────────────────────────────────┼───────────────┘  │
│                                           │                  │
└───────────────────────────────────────────┼──────────────────┘
                                            │ HTTP API
┌───────────────────────────────────────────┼──────────────────┐
│                    User Space             │                  │
│                                           ▼                  │
│  ┌──────────────────────────────────────────────────────┐  │
│  │      Spring Boot Server (Java)                        │  │
│  │  ┌──────────────┐         ┌──────────────────────┐  │  │
│  │  │ REST API     │         │  PostgreSQL Database │  │  │
│  │  │ Controller   │◄───────►│  (File Metadata &    │  │  │
│  │  │              │         │   File Data Chunks)  │  │  │
│  │  └──────────────┘         └──────────────────────┘  │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

## ✨ Features

### File System Operations
- ✅ Create, read, write, and delete files
- ✅ Create and remove directories
- ✅ Hard link support (multiple directory entries pointing to same file)
- ✅ Directory listing and traversal
- ✅ File permissions (mode bits)
- ✅ File size tracking

### Operational Modes
- ✅ **RAM Mode**: Fast in-memory storage (no persistence)
- ✅ **Server Mode**: Persistent storage with remote synchronization
- ✅ Seamless switching between modes via mount options

### Server Features
- ✅ Multi-tenant support (token-based isolation)
- ✅ Efficient data storage (chunked file data)
- ✅ Transactional operations
- ✅ RESTful API design

## 🔧 Components

### 1. Kernel Module (`source/vtfs.c`)
The core file system implementation providing:
- VFS (Virtual File System) interface integration
- Inode operations (lookup, create, unlink, mkdir, rmdir, link)
- File operations (open, read, write)
- Directory iteration
- Server integration hooks

### 2. HTTP Client (`source/http.c`, `source/http.h`)
Kernel-space HTTP client for server communication:
- HTTP GET request construction
- Response parsing
- Base64 encoding/decoding for binary data
- Error handling

### 3. Spring Boot Server (`server/`)
REST API server providing persistent storage:
- **Controller**: REST endpoints (`/api/list`, `/api/create`, `/api/read`, `/api/write`, etc.)
- **Service**: Business logic for file operations
- **Repository**: JPA repositories for database access
- **Models**: `VtfsFile` (metadata) and `FileData` (chunked data)

### 4. Database Schema
PostgreSQL tables:
- `vtfs_files`: File metadata (ino, name, parent_ino, mode, nlink, data_size)
- `vtfs_file_data`: File data chunks (token, ino, offset, data)

## 📦 Requirements

### Kernel Module
- Linux kernel with headers (for module compilation)
- GCC compiler
- Make

### Server
- Java 17+
- Maven 3.6+
- PostgreSQL 12+
- Spring Boot 3.2.0

### Runtime
- Root privileges (for kernel module operations)

## 🔨 Building

### Build Kernel Module

```bash
# Compile the kernel module
make

# Clean build artifacts
make clean
```

This will generate `vtfs.ko` kernel module file.

### Build Server

```bash
cd server
mvn clean package
```

The server JAR will be created in `server/target/vtfs-server-1.0.0.jar`.

## 📥 Installation

### 1. Setup PostgreSQL Database

```bash
# Create database and user
sudo -u postgres psql << EOF
CREATE DATABASE vtfs_db;
CREATE USER vtfs_user WITH PASSWORD 'vtfs_password';
GRANT ALL PRIVILEGES ON DATABASE vtfs_db TO vtfs_user;
\q
EOF
```

### 2. Configure Server

Edit `server/src/main/resources/application.properties` if needed:
```properties
spring.datasource.url=jdbc:postgresql://localhost:5432/vtfs_db
spring.datasource.username=vtfs_user
spring.datasource.password=vtfs_password
server.port=8080
```

### 3. Start Server

```bash
cd server
mvn spring-boot:run
```

Or run the JAR:
```bash
java -jar target/vtfs-server-1.0.0.jar
```

### 4. Load Kernel Module

```bash
# Load the module
sudo insmod vtfs.ko

# Verify it's loaded
lsmod | grep vtfs
```

## 🚀 Usage

### RAM Mode (In-Memory)

```bash
# Create mount point
sudo mkdir -p /mnt/vtfs

# Mount in RAM mode (empty token)
sudo mount -t vtfs none /mnt/vtfs -o token=""

# Use the file system
echo "Hello VTFS" > /mnt/vtfs/test.txt
cat /mnt/vtfs/test.txt
ls -la /mnt/vtfs

# Unmount
sudo umount /mnt/vtfs
```

### Server Mode (Persistent)

```bash
# Mount with token (unique identifier for your file system instance)
sudo mount -t vtfs none /mnt/vtfs -o token="my_unique_token"

# Create files and directories
mkdir /mnt/vtfs/documents
echo "Persistent data" > /mnt/vtfs/documents/file.txt

# Unmount - data persists on server
sudo umount /mnt/vtfs

# Remount with same token - data is restored
sudo mount -t vtfs none /mnt/vtfs -o token="my_unique_token"
cat /mnt/vtfs/documents/file.txt  # Data is still there!
```

### Unload Module

```bash
# Unmount all instances first
sudo umount /mnt/vtfs

# Remove module
sudo rmmod vtfs
```

## 📡 API Reference

The server exposes REST endpoints at `http://localhost:8080/api/`:

### List Files
```
GET /api/list?token={token}&parent_ino={parent_ino}
```
Returns list of files in directory.

### Create File
```
GET /api/create?token={token}&parent_ino={parent_ino}&name={name}&mode={mode}
```
Creates a new file. Returns: `ino,mode\n`

### Read File
```
GET /api/read?token={token}&ino={ino}&offset={offset}&length={length}
```
Reads file data. Returns: `[8-byte error code][data]`

### Write File
```
GET /api/write?token={token}&ino={ino}&offset={offset}&data={base64_encoded_data}
```
Writes data to file. Data must be Base64 encoded.

### Delete File
```
GET /api/delete?token={token}&ino={ino}
```
Deletes a file.

### Create Directory
```
GET /api/mkdir?token={token}&parent_ino={parent_ino}&name={name}&mode={mode}
```
Creates a directory.

### Remove Directory
```
GET /api/rmdir?token={token}&ino={ino}
```
Removes an empty directory.

### Create Hard Link
```
GET /api/link?token={token}&old_ino={old_ino}&parent_ino={parent_ino}&name={name}
```
Creates a hard link.

### Unlink
```
GET /api/unlink?token={token}&ino={ino}
```
Removes a hard link.

**Response Format**: All responses start with an 8-byte big-endian error code (0 = success).

## 🧪 Testing

### Run Kernel Module Tests

```bash
# Test RAM mode and Server mode
sudo ./test_vtfs.sh
```

### Run Server Integration Tests

```bash
# Test persistence across unmount/remount
sudo ./test_server_integration.sh
```

### Manual Testing

```bash
# 1. Start server
cd server && mvn spring-boot:run

# 2. In another terminal, compile and test
make
sudo insmod vtfs.ko
sudo mkdir -p /mnt/vtfs
sudo mount -t vtfs none /mnt/vtfs -o token="test123"

# 3. Test operations
echo "test" > /mnt/vtfs/file.txt
mkdir /mnt/vtfs/dir
ls -la /mnt/vtfs

# 4. Test persistence
sudo umount /mnt/vtfs
sudo mount -t vtfs none /mnt/vtfs -o token="test123"
cat /mnt/vtfs/file.txt  # Should still exist
```

## 📁 Project Structure

```
.
├── source/                 # Kernel module source code
│   ├── vtfs.c             # Main file system implementation
│   ├── http.c             # HTTP client implementation
│   └── http.h             # HTTP client header
├── server/                 # Spring Boot server
│   ├── src/main/java/com/vtfs/
│   │   ├── controller/    # REST API controllers
│   │   ├── service/       # Business logic
│   │   ├── repository/    # JPA repositories
│   │   └── model/         # Data models
│   └── pom.xml            # Maven configuration
├── Makefile               # Kernel module build configuration
├── test_vtfs.sh           # Comprehensive test script
├── test_server_integration.sh  # Server integration tests
└── README.md              # This file
```

## 🔒 Security Considerations

- The kernel module requires root privileges to load and mount
- Server mode uses token-based isolation (each token is a separate namespace)
- No authentication is implemented - tokens should be kept secret
- File permissions are stored but not enforced by the kernel module
- For production use, implement proper authentication and authorization

## 🐛 Troubleshooting

### Module won't load
- Check kernel version compatibility
- Verify kernel headers are installed: `apt-get install linux-headers-$(uname -r)`
- Check dmesg for errors: `dmesg | tail`

### Server connection fails
- Verify server is running: `curl http://localhost:8080/api/list?token=test&parent_ino=100`
- Check firewall settings
- Verify PostgreSQL is running and accessible

### Files not persisting
- Ensure you're using Server mode (non-empty token)
- Check server logs for errors
- Verify database connection in `application.properties`

## 📝 License

This project is licensed under the GPL v2 License (kernel module) - see the module source for details.

## 👤 Author

Developed as a learning project for Linux kernel development and distributed systems.

---

## 📦 Repository

**Suggested Repository Name**: `vtfs` or `vtfs-virtual-filesystem`

The current repository name doesn't reflect the project's purpose. Consider renaming it to better represent this Virtual File System implementation.

**Note**: This is an educational project. Use at your own risk in production environments.
