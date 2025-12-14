# Distributed File System (DFS) - Linux Version

A mini distributed file system implementation in C++ for Linux that demonstrates OS-level concepts including process management, TCP sockets, file I/O, fault tolerance, and data integrity.

## Features

- **Distributed Storage**: Files are stored across multiple storage nodes
- **Replication**: Each file is automatically replicated to 2 nodes for fault tolerance
- **Fault Tolerance**: System continues to work even when one node fails
- **Data Integrity**: Checksum verification ensures data correctness
- **Terminal-based**: Fully operable from command line
- **Linux System Calls**: Uses POSIX sockets, `kill()` for process checking, `getpid()` for process IDs

## Architecture

```
./client  <───TCP───>  ./coordinator
                           |
                           |
                ┌──────────┴──────────┐
                │                     │
            ./node 1              ./node 2
```

- **Coordinator**: Metadata server that manages file locations and node health
- **Storage Nodes**: Independent processes that store and serve files
- **Client**: Command-line interface for uploading, downloading, and listing files

## Prerequisites

- Linux system (Ubuntu, Debian, Fedora, etc.)
- g++ compiler (C++17 support)
- Standard C++ filesystem library support

Install build tools on Ubuntu/Debian:
```bash
sudo apt update
sudo apt install build-essential
```

## Building

### Using Makefile (Recommended)

```bash
cd Linux
make
```

This will build all three executables:
- `coordinator`
- `node`
- `client`

### Manual Build

```bash
# Build coordinator
g++ -std=c++17 coordinator/coordinator.cpp -o coordinator

# Build node
g++ -std=c++17 node/node.cpp -o node

# Build client
g++ -std=c++17 client/client.cpp -o client
```

### Clean Build Artifacts

```bash
make clean
```

## Usage

### Quick Start (All in One Terminal)

```bash
# Make scripts executable (first time only)
chmod +x start.sh stop.sh

# Start everything at once
./start.sh

# Then use client in the same terminal
./client upload test.txt /docs/test.txt
./client download /docs/test.txt output.txt
./client list

# Stop everything
./stop.sh
```

The start script runs coordinator and nodes in the background, so you can use the client in the same terminal!

### Manual Start (Separate Terminals)

#### Step 1: Start the Coordinator

Open a terminal and run:

```bash
./coordinator
```

The coordinator will start listening on port 9000.

#### Step 2: Start Storage Nodes

Open separate terminals for each node (you can create as many as needed):

```bash
# Terminal 2
./node 1

# Terminal 3
./node 2

# Terminal 4 (optional)
./node 3

# Terminal 5 (optional)
./node 4

# ... and so on
```

Each node will:
- Register itself with the coordinator
- Listen on port (9001 + nodeId), e.g., node 1 on 9001, node 2 on 9002, node 3 on 9003, etc.
- Create storage folders: `storage/node1/`, `storage/node2/`, `storage/node3/`, etc.

#### Step 3: Use the Client

In another terminal, use the client to interact with the DFS:

```bash
# Upload a file
./client upload test.txt /docs/test.txt

# List all files
./client list

# Download a file
./client download /docs/test.txt output.txt
```

## Fault Tolerance Demo

This is the **impressive demo** for faculty:

1. Start coordinator and both nodes (as above)
2. Upload a file:
   ```bash
   ./client upload test.txt /docs/test.txt
   ```
   Output: `File stored on Node 1 and Node 2`

3. Kill one node:
   ```bash
   kill -9 <pid_of_node1>
   ```
   Or find the process:
   ```bash
   ps aux | grep node
   kill -9 <pid>
   ```

4. Download the file:
   ```bash
   ./client download /docs/test.txt output.txt
   ```
   Output:
   ```
   Node 1 failed, recovered using replica
   File downloaded successfully: output.txt
   ```

This demonstrates that the system **survives node failures** and automatically recovers using replicas.

## System Calls Used

| Purpose           | System Calls / APIs                          |
| ----------------- | -------------------------------------------- |
| Process creation  | Multiple executables (Linux approach)       |
| IPC               | `socket()`, `bind()`, `listen()`, `accept()` |
| File ops          | `fstream` (C++), `open()`, `read()`, `write()` |
| Directory ops     | `filesystem` (C++17), `mkdir()`               |
| Failure detection | `kill(pid, 0)` - returns 0 if process exists |
| Process ID        | `getpid()` - get current process ID          |
| Data integrity    | Checksum calculation                         |

## Project Structure

```
Linux/
│
├── coordinator/
│   └── coordinator.cpp    # Metadata server
│
├── node/
│   └── node.cpp           # Storage node
│
├── client/
│   └── client.cpp         # Client CLI
│
├── storage/
│   ├── node1/             # Node 1 storage folder
│   └── node2/             # Node 2 storage folder
│
├── Makefile               # Build configuration
└── README.md             # This file
```

## How It Works

### Upload Process

1. Client sends `UPLOAD <dfs_path>` to coordinator
2. Coordinator receives file data and calculates checksum
3. Coordinator stores file on 2 available nodes
4. Coordinator updates metadata table
5. Returns success message with node IDs

### Download Process

1. Client sends `DOWNLOAD <dfs_path>` to coordinator
2. Coordinator looks up file in metadata table
3. Coordinator checks which nodes are alive using `kill(pid, 0)`
4. If primary node is down, uses replica node
5. Retrieves file from node and verifies checksum
6. Sends file to client

### Fault Detection

- Coordinator tracks node process IDs
- Uses `kill(pid, 0)` to check if process is alive
  - Returns 0 if process exists
  - Returns -1 if process doesn't exist
- When a node fails, automatically redirects requests to replica

### Data Integrity

- Every file has a checksum calculated using a simple sum algorithm
- Checksums are verified:
  - When storing files on nodes
  - When retrieving files from nodes
  - When client receives files

## Linux-Specific Features

- **POSIX Sockets**: Uses standard Linux socket API
- **Process Management**: Uses `kill()` and `getpid()` system calls
- **No WSA**: Unlike Windows, no socket library initialization needed
- **Signal Handling**: Uses `kill(pid, 0)` for process health checks

## Limitations

- Maximum file size: 10MB (configurable in coordinator.cpp)
- Supports unlimited nodes (limited only by available ports: node ID + 9001 must be < 65535)
- Single coordinator (no coordinator replication)
- No authentication/authorization
- No encryption

## Troubleshooting

### Port already in use
```bash
# Check what's using the port
sudo netstat -tulpn | grep 9000
# Or
sudo lsof -i :9000

# Kill the process if needed
kill -9 <pid>
```

### Permission denied
```bash
# Make sure executables have execute permission
chmod +x coordinator node client
```

### Cannot connect to coordinator
- Ensure coordinator is running before starting nodes
- Check firewall settings:
  ```bash
  sudo ufw allow 9000/tcp
  sudo ufw allow 9001:9010/tcp  # For nodes
  ```

### File not found errors
- Verify the DFS path exists using `./client list`
- Check that nodes are running and registered
- Verify storage directories exist: `ls -la storage/`

## Viva Explanation

> "The system stores files across multiple nodes with replication. The coordinator maintains metadata and detects node failures using the `kill(pid, 0)` system call. When a node fails, the client automatically retrieves the file from a replica node, ensuring availability and integrity. All communication uses POSIX TCP sockets, and the system uses Linux system calls for process management and file operations."

## Differences from Windows Version

| Feature | Windows | Linux |
|---------|---------|-------|
| Socket Type | `SOCKET` (typedef) | `int` |
| Socket Close | `closesocket()` | `close()` |
| Socket Init | `WSAStartup()` | Not needed |
| Process Check | `OpenProcess()` + `GetExitCodeProcess()` | `kill(pid, 0)` |
| Process ID | `GetCurrentProcessId()` | `getpid()` |
| Library | `ws2_32.lib` | None needed |

## License

This is an educational project for OS course evaluation.

