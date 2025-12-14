#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <cstring>

using namespace std;

struct FileEntry {
    string filename;
    int node1;
    int node2;
    unsigned long checksum;
};

map<string, FileEntry> fileTable; // DFS path → FileEntry
map<int, pid_t> nodePids; // nodeId → process ID
map<int, bool> nodeAlive; // nodeId → alive status

const int COORDINATOR_PORT = 9000;
const int NODE_BASE_PORT = 9001;

// Simple checksum function
unsigned long calculateChecksum(const char* data, int size) {
    unsigned long sum = 0;
    for (int i = 0; i < size; i++) {
        sum += (unsigned char)data[i];
    }
    return sum;
}

// Check if a process is alive (Linux: use kill(pid, 0))
bool isNodeAlive(int nodeId) {
    if (nodePids.find(nodeId) == nodePids.end()) {
        return false;
    }
    
    pid_t pid = nodePids[nodeId];
    // kill(pid, 0) returns 0 if process exists, -1 if it doesn't
    if (kill(pid, 0) == 0) {
        return true;
    }
    return false;
}

// Update node status
void updateNodeStatus() {
    for (auto& pair : nodePids) {
        nodeAlive[pair.first] = isNodeAlive(pair.first);
    }
}

// Forward declaration
bool sendFileToNode(int nodeId, const string& dfsPath, const char* data, int size, unsigned long checksum);

// Handle UPLOAD command
string handleUpload(int clientSock, const string& dfsPath) {
    updateNodeStatus();
    
    // Find all alive nodes (supports unlimited nodes)
    vector<int> availableNodes;
    for (auto& pair : nodeAlive) {
        if (pair.second) {
            availableNodes.push_back(pair.first);
        }
    }
    
    if (availableNodes.size() < 2) {
        return "ERROR: Not enough alive nodes (need at least 2, found " + to_string(availableNodes.size()) + ")";
    }
    
    // Receive file data
    char sizeBuf[16] = {0};
    recv(clientSock, sizeBuf, sizeof(sizeBuf), 0);
    int fileSize = atoi(sizeBuf);
    
    if (fileSize <= 0 || fileSize > 10 * 1024 * 1024) { // Max 10MB
        return "ERROR: Invalid file size";
    }
    
    char* fileData = new char[fileSize];
    int totalReceived = 0;
    while (totalReceived < fileSize) {
        int received = recv(clientSock, fileData + totalReceived, fileSize - totalReceived, 0);
        if (received <= 0) {
            delete[] fileData;
            return "ERROR: Failed to receive file data";
        }
        totalReceived += received;
    }
    
    // Calculate checksum
    unsigned long checksum = calculateChecksum(fileData, fileSize);
    
    // Select two nodes for replication (simple round-robin: first two available)
    // With many nodes, this distributes load across all nodes
    int node1 = availableNodes[0];
    int node2 = availableNodes[1];
    
    bool node1Success = sendFileToNode(node1, dfsPath, fileData, fileSize, checksum);
    bool node2Success = sendFileToNode(node2, dfsPath, fileData, fileSize, checksum);
    
    delete[] fileData;
    
    if (!node1Success || !node2Success) {
        return "ERROR: Failed to store file on nodes";
    }
    
    // Update metadata
    FileEntry entry;
    entry.filename = dfsPath;
    entry.node1 = node1;
    entry.node2 = node2;
    entry.checksum = checksum;
    fileTable[dfsPath] = entry;
    
    return "STORED " + to_string(node1) + " " + to_string(node2);
}

// Send file to a storage node
bool sendFileToNode(int nodeId, const string& dfsPath, const char* data, int size, unsigned long checksum) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        return false;
    }
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(NODE_BASE_PORT + nodeId);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        close(sock);
        return false;
    }
    
    // Send STORE command
    string cmd = "STORE " + dfsPath + " " + to_string(size) + " " + to_string(checksum) + "\n";
    send(sock, cmd.c_str(), cmd.size(), 0);
    
    // Send file data
    int totalSent = 0;
    while (totalSent < size) {
        int sent = send(sock, data + totalSent, size - totalSent, 0);
        if (sent <= 0) {
            close(sock);
            return false;
        }
        totalSent += sent;
    }
    
    // Wait for confirmation
    char response[256] = {0};
    recv(sock, response, sizeof(response), 0);
    
    close(sock);
    return string(response).find("OK") != string::npos;
}

// Handle DOWNLOAD command
string handleDownload(int clientSock, const string& dfsPath) {
    updateNodeStatus();
    
    if (fileTable.find(dfsPath) == fileTable.end()) {
        return "ERROR: File not found";
    }
    
    FileEntry entry = fileTable[dfsPath];
    
    // Try node1 first
    bool node1Alive = nodeAlive[entry.node1];
    bool node2Alive = nodeAlive[entry.node2];
    
    int nodeToUse = -1;
    string recoveryMsg = "";
    
    if (node1Alive) {
        nodeToUse = entry.node1;
    } else if (node2Alive) {
        nodeToUse = entry.node2;
        recoveryMsg = "Node " + to_string(entry.node1) + " failed, recovered using replica";
    } else {
        return "ERROR: Both nodes are down";
    }
    
    // Retrieve file from node
    int nodeSock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(NODE_BASE_PORT + nodeToUse);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    
    if (connect(nodeSock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        close(nodeSock);
        return "ERROR: Cannot connect to node";
    }
    
    // Send GET command
    string cmd = "GET " + dfsPath + "\n";
    send(nodeSock, cmd.c_str(), cmd.size(), 0);
    
    // Receive file size
    char sizeBuf[16] = {0};
    recv(nodeSock, sizeBuf, sizeof(sizeBuf), 0);
    int fileSize = atoi(sizeBuf);
    
    if (fileSize <= 0) {
        close(nodeSock);
        return "ERROR: Invalid file size from node";
    }
    
    // Receive checksum
    char checksumBuf[32] = {0};
    recv(nodeSock, checksumBuf, sizeof(checksumBuf), 0);
    unsigned long receivedChecksum = strtoul(checksumBuf, NULL, 10);
    
    // Receive file data
    char* fileData = new char[fileSize];
    int totalReceived = 0;
    while (totalReceived < fileSize) {
        int received = recv(nodeSock, fileData + totalReceived, fileSize - totalReceived, 0);
        if (received <= 0) {
            delete[] fileData;
            close(nodeSock);
            return "ERROR: Failed to receive file data";
        }
        totalReceived += received;
    }
    
    close(nodeSock);
    
    // Verify checksum
    unsigned long calculatedChecksum = calculateChecksum(fileData, fileSize);
    if (calculatedChecksum != receivedChecksum) {
        delete[] fileData;
        return "ERROR: Checksum mismatch - data corruption detected";
    }
    
    // Send to client
    string response = "OK " + to_string(fileSize) + " " + to_string(calculatedChecksum) + "\n";
    if (!recoveryMsg.empty()) {
        response = recoveryMsg + "\n" + response;
    }
    send(clientSock, response.c_str(), response.size(), 0);
    
    // Send file data
    int totalSent = 0;
    while (totalSent < fileSize) {
        int sent = send(clientSock, fileData + totalSent, fileSize - totalSent, 0);
        if (sent <= 0) {
            delete[] fileData;
            return "ERROR: Failed to send file to client";
        }
        totalSent += sent;
    }
    
    delete[] fileData;
    return "SUCCESS";
}

// Handle LIST command
string handleList() {
    string result = "";
    for (auto& pair : fileTable) {
        result += pair.first + "\n";
    }
    if (result.empty()) {
        result = "No files stored\n";
    }
    return result;
}

// Handle REGISTER command (nodes register themselves)
string handleRegister(int clientSock) {
    char buffer[256] = {0};
    recv(clientSock, buffer, sizeof(buffer), 0);
    
    stringstream ss(buffer);
    string cmd;
    int nodeId;
    pid_t pid;
    
    ss >> cmd >> nodeId >> pid;
    
    nodePids[nodeId] = pid;
    nodeAlive[nodeId] = true;
    
    return "REGISTERED " + to_string(nodeId);
}

int main() {
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == -1) {
        cerr << "Socket creation failed\n";
        return 1;
    }
    
    // Set socket option to reuse address
    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(COORDINATOR_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(server, (sockaddr*)&addr, sizeof(addr)) != 0) {
        cerr << "Bind failed\n";
        close(server);
        return 1;
    }
    
    if (listen(server, 5) != 0) {
        cerr << "Listen failed\n";
        close(server);
        return 1;
    }
    
    cout << "Coordinator running on port " << COORDINATOR_PORT << "...\n";
    cout << "Waiting for nodes and clients...\n";
    
    while (true) {
        sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int client = accept(server, (sockaddr*)&clientAddr, &clientLen);
        if (client == -1) {
            continue;
        }
        
        char buffer[1024] = {0};
        int received = recv(client, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) {
            close(client);
            continue;
        }
        
        string cmd(buffer);
        string response;
        
        if (cmd.find("REGISTER") == 0) {
            response = handleRegister(client);
            send(client, response.c_str(), response.size(), 0);
        }
        else if (cmd.find("UPLOAD") == 0) {
            stringstream ss(cmd);
            string upload, dfsPath;
            ss >> upload >> dfsPath;
            response = handleUpload(client, dfsPath);
            send(client, response.c_str(), response.size(), 0);
        }
        else if (cmd.find("DOWNLOAD") == 0) {
            stringstream ss(cmd);
            string download, dfsPath;
            ss >> download >> dfsPath;
            handleDownload(client, dfsPath);
        }
        else if (cmd.find("LIST") == 0) {
            response = handleList();
            send(client, response.c_str(), response.size(), 0);
        }
        else {
            response = "ERROR: Unknown command";
            send(client, response.c_str(), response.size(), 0);
        }
        
        close(client);
    }
    
    close(server);
    return 0;
}

