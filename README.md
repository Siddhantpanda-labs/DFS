# Distributed File System (DFS)

A mini distributed file system implementation in C++ for Windows that demonstrates OS-level concepts including process management, TCP sockets, file I/O, fault tolerance, and data integrity.

## Features

- **Distributed Storage**: Files are stored across multiple storage nodes
- **Replication**: Each file is automatically replicated to 2 nodes for fault tolerance
- **Fault Tolerance**: System continues to work even when one node fails
- **Data Integrity**: Checksum verification ensures data correctness
- **Terminal-based**: Fully operable from command line

## Architecture

```
Client.exe  <───TCP───>  Coordinator.exe
                             |
                             |
                  ┌──────────┴──────────┐
                  │                     │
              Node1.exe             Node2.exe
```

- **Coordinator**: Metadata server that manages file locations and node health
- **Storage Nodes**: Independent processes that store and serve files
- **Client**: Command-line interface for uploading, downloading, and listing files

## Prerequisites

- Windows 10/11
- MinGW-w64 (g++ compiler)
- Or Visual Studio with C++ support

## Building

### Using MinGW-w64 (g++)

```bash
# Build coordinator
g++ -std=c++17 coordinator/coordinator.cpp -o coordinator.exe -lws2_32

# Build node
g++ -std=c++17 node/node.cpp -o node.exe -lws2_32

# Build client
g++ -std=c++17 client/client.cpp -o client.exe -lws2_32
```

### Using Visual Studio

1. Create a new C++ project for each component
2. Add the respective .cpp file
3. Link against `ws2_32.lib` (Winsock library)
4. Set C++ standard to C++17 or later

## Usage

### Quick Start (All in One Terminal)

**Windows:**
```bash
# Start everything at once
start_windows.bat

# Then use client in the same terminal
client.exe upload test.txt /docs/test.txt
client.exe download /docs/test.txt output.txt
client.exe list

# Stop everything
stop_windows.bat
```

### Manual Start (Separate Terminals)

#### Step 1: Start the Coordinator

Open a terminal and run:

```bash
coordinator.exe
```

The coordinator will start listening on port 9000.

#### Step 2: Start Storage Nodes

Open separate terminals for each node (you can create as many as needed):

```bash
# Terminal 2
node.exe 1

# Terminal 3
node.exe 2

# Terminal 4 (optional)
node.exe 3

# Terminal 5 (optional)
node.exe 4

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
client.exe upload test.txt /docs/test.txt

# List all files
client.exe list

# Download a file
client.exe download /docs/test.txt output.txt
```

## Fault Tolerance Demo

This is the **impressive demo** for faculty:

1. Start coordinator and both nodes (as above)
2. Upload a file:
   ```bash
   client.exe upload test.txt /docs/test.txt
   ```
   Output: `File stored on Node 1 and Node 2`

3. Kill one node (find its PID and kill it, or use Task Manager)

4. Download the file:
   ```bash
   client.exe download /docs/test.txt output.txt
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
| Process creation  | Multiple executables (Windows approach)      |
| IPC               | `socket()`, `bind()`, `listen()`, `accept()` |
| File ops          | `fstream` (C++), `open()`, `read()`, `write()` |
| Directory ops     | `filesystem` (C++17), `mkdir()`               |
| Failure detection | `OpenProcess()`, `GetExitCodeProcess()`      |
| Data integrity    | Checksum calculation                         |

## Project Structure

```
DFS/
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
├── plan.md                # Detailed implementation plan
└── README.md              # This file
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
3. Coordinator checks which nodes are alive
4. If primary node is down, uses replica node
5. Retrieves file from node and verifies checksum
6. Sends file to client

### Fault Detection

- Coordinator tracks node process IDs
- Periodically checks if nodes are alive using `GetExitCodeProcess()`
- When a node fails, automatically redirects requests to replica

### Data Integrity

- Every file has a checksum calculated using a simple sum algorithm
- Checksums are verified:
  - When storing files on nodes
  - When retrieving files from nodes
  - When client receives files

## Limitations

- Maximum file size: 10MB (configurable in coordinator.cpp)
- Supports unlimited nodes (limited only by available ports: node ID + 9001 must be < 65535)
- Single coordinator (no coordinator replication)
- No authentication/authorization
- No encryption

## Viva Explanation

> "The system stores files across multiple nodes with replication. The coordinator maintains metadata and detects node failures. When a node fails, the client automatically retrieves the file from a replica node, ensuring availability and integrity. All communication uses TCP sockets, and the system uses OS-level APIs for process management and file operations."

## Troubleshooting

### Port already in use
- Make sure no other program is using ports 9000-9010
- Close any previous instances of coordinator/node

### Cannot connect to coordinator
- Ensure coordinator is running before starting nodes
- Check firewall settings

### File not found errors
- Verify the DFS path exists using `client.exe list`
- Check that nodes are running and registered

## License

This is an educational project for OS course evaluation.

