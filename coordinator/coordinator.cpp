#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

using namespace std;

// Constants
const int COORDINATOR_PORT = 9000;
const int NODE_BASE_PORT = 9001;
const DWORD SOCKET_TIMEOUT = 5000;  // 5 seconds
const DWORD FILE_TRANSFER_TIMEOUT = 30000;  // 30 seconds
const int MAX_FILE_SIZE = 10 * 1024 * 1024;  // 10 MB

// Data structures
struct FileEntry {
    string filename;
    vector<int> nodeIds;
    unsigned long checksum;
};

map<string, FileEntry> fileTable;
map<int, DWORD> nodePids;
map<int, bool> nodeAlive;

// Utility functions
unsigned long calculateChecksum(const char* data, int size) {
    unsigned long sum = 0;
    for (int i = 0; i < size; i++) {
        sum += (unsigned char)data[i];
    }
    return sum;
}

bool isNodeAlive(int nodeId) {
    if (nodePids.find(nodeId) == nodePids.end()) return false;
    DWORD pid = nodePids[nodeId];
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (hProcess == NULL) return false;
    DWORD exitCode;
    GetExitCodeProcess(hProcess, &exitCode);
    CloseHandle(hProcess);
    return exitCode == STILL_ACTIVE;
}

void updateNodeStatus() {
    for (auto& pair : nodePids) {
        nodeAlive[pair.first] = isNodeAlive(pair.first);
    }
}

void setSocketTimeouts(SOCKET sock, DWORD timeout) {
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
}

SOCKET connectToNode(int nodeId, DWORD timeout = SOCKET_TIMEOUT) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return INVALID_SOCKET;
    
    setSocketTimeouts(sock, timeout);
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(NODE_BASE_PORT + nodeId);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    
    return sock;
}

// Node communication functions
bool sendFileToNode(int nodeId, const string& dfsPath, const char* data, int size, unsigned long checksum) {
    SOCKET sock = connectToNode(nodeId, FILE_TRANSFER_TIMEOUT);
    if (sock == INVALID_SOCKET) return false;
    
    string cmd = "STORE " + dfsPath + " " + to_string(size) + " " + to_string(checksum) + "\n";
    int cmdSent = send(sock, cmd.c_str(), cmd.size(), 0);
    if (cmdSent <= 0) {
        closesocket(sock);
        return false;
    }
    
    // Send file data
    int totalSent = 0;
    while (totalSent < size) {
        int sent = send(sock, data + totalSent, size - totalSent, 0);
        if (sent <= 0) {
            closesocket(sock);
            return false;
        }
        totalSent += sent;
    }
    
    // Wait for response
    char response[256] = {0};
    int received = recv(sock, response, sizeof(response) - 1, 0);
    closesocket(sock);
    
    if (received <= 0) return false;
    response[received] = '\0';
    return string(response).find("OK") != string::npos;
}

bool deleteFileFromNode(int nodeId, const string& dfsPath) {
    SOCKET sock = connectToNode(nodeId);
    if (sock == INVALID_SOCKET) return false;
    
    string cmd = "DELETE " + dfsPath + "\n";
    if (send(sock, cmd.c_str(), cmd.size(), 0) <= 0) {
        closesocket(sock);
        return false;
    }
    
    char response[256] = {0};
    int received = recv(sock, response, sizeof(response), 0);
    closesocket(sock);
    
    if (received <= 0) return false;
    response[received] = '\0';
    return string(response).find("OK") != string::npos;
}

// Command handlers
string handleRegister(const string& cmd) {
    stringstream ss(cmd);
    string registerCmd;
    int nodeId;
    DWORD pid;
    ss >> registerCmd >> nodeId >> pid;
    
    nodePids[nodeId] = pid;
    nodeAlive[nodeId] = true;
    
    cout << "Node " << nodeId << " registered (PID: " << pid << ")\n";
    return "REGISTERED " + to_string(nodeId);
}

string handleUpload(SOCKET clientSock, const string& dfsPath, const string& remainingData) {
    updateNodeStatus();
    
    vector<int> availableNodes;
    for (auto& pair : nodeAlive) {
        if (pair.second) availableNodes.push_back(pair.first);
    }
    
    if (availableNodes.empty()) {
        return "ERROR: No nodes available";
    }
    
    // Read file size
    string dataBuffer = remainingData;
    string sizeLine;
    size_t sizeEnd = dataBuffer.find('\n');
    
    if (sizeEnd != string::npos) {
        sizeLine = dataBuffer.substr(0, sizeEnd);
        dataBuffer = dataBuffer.substr(sizeEnd + 1);
    } else {
        setSocketTimeouts(clientSock, SOCKET_TIMEOUT);
        char sizeBuf[32] = {0};
        int sizePos = 0;
        
        if (!dataBuffer.empty()) {
            int toCopy = min((int)dataBuffer.length(), (int)sizeof(sizeBuf) - 1);
            memcpy(sizeBuf, dataBuffer.c_str(), toCopy);
            sizePos = toCopy;
            dataBuffer = "";
        }
        
        while (sizePos < sizeof(sizeBuf) - 1) {
            int received = recv(clientSock, sizeBuf + sizePos, 1, 0);
            if (received <= 0) {
                return "ERROR: Failed to receive file size";
            }
            if (sizeBuf[sizePos] == '\n') {
                sizeBuf[sizePos] = '\0';
                break;
            }
            sizePos++;
        }
        sizeLine = string(sizeBuf);
    }
    
    int fileSize = atoi(sizeLine.c_str());
    if (fileSize <= 0 || fileSize > MAX_FILE_SIZE) {
        return "ERROR: Invalid file size";
    }
    
    // Read file data
    char* fileData = new char[fileSize];
    int totalReceived = 0;
    
    if (!dataBuffer.empty()) {
        int toCopy = min((int)dataBuffer.length(), fileSize);
        memcpy(fileData, dataBuffer.c_str(), toCopy);
        totalReceived = toCopy;
    }
    
    setSocketTimeouts(clientSock, FILE_TRANSFER_TIMEOUT);
    while (totalReceived < fileSize) {
        int received = recv(clientSock, fileData + totalReceived, fileSize - totalReceived, 0);
        if (received <= 0) {
            delete[] fileData;
            return "ERROR: Failed to receive file";
        }
        totalReceived += received;
    }
    
    unsigned long checksum = calculateChecksum(fileData, fileSize);
    
    // Store on all available nodes
    vector<int> successfulNodes;
    for (int nodeId : availableNodes) {
        if (sendFileToNode(nodeId, dfsPath, fileData, fileSize, checksum)) {
            successfulNodes.push_back(nodeId);
        }
    }
    
    delete[] fileData;
    
    if (successfulNodes.empty()) {
        return "ERROR: Failed to store on any node";
    }
    
    // Update file table
    FileEntry entry;
    entry.filename = dfsPath;
    entry.nodeIds = successfulNodes;
    entry.checksum = checksum;
    fileTable[dfsPath] = entry;
    
    // Log
    string nodeList;
    for (size_t i = 0; i < successfulNodes.size(); i++) {
        if (i > 0) nodeList += ", ";
        nodeList += "Node " + to_string(successfulNodes[i]);
    }
    cout << "UPLOAD: " << dfsPath << " -> " << nodeList << "\n";
    
    // Build response
    string result = "STORED";
    for (int nodeId : successfulNodes) {
        result += " " + to_string(nodeId);
    }
    return result;
}

string handleDownload(SOCKET clientSock, const string& dfsPath) {
    updateNodeStatus();
    
    if (fileTable.find(dfsPath) == fileTable.end()) {
        return "ERROR: File not found";
    }
    
    FileEntry entry = fileTable[dfsPath];
    
    // Find first alive node
    int nodeToUse = -1;
    string recoveryMsg;
    
    for (size_t i = 0; i < entry.nodeIds.size(); i++) {
        int nodeId = entry.nodeIds[i];
        if (nodeAlive[nodeId]) {
            nodeToUse = nodeId;
            if (i > 0) {
                recoveryMsg = "Node " + to_string(entry.nodeIds[0]) + " failed, recovered using replica on Node " + to_string(nodeToUse) + "\n";
                cout << "FAULT TOLERANCE: Node " << entry.nodeIds[0] << " down, using Node " << nodeToUse << "\n";
            }
            break;
        }
    }
    
    if (nodeToUse == -1) {
        return "ERROR: All nodes down";
    }
    
    // Connect to node
    SOCKET nodeSock = connectToNode(nodeToUse);
    if (nodeSock == INVALID_SOCKET) {
        return "ERROR: Cannot connect to node";
    }
    
    // Request file
    string cmd = "GET " + dfsPath + "\n";
    if (send(nodeSock, cmd.c_str(), cmd.size(), 0) <= 0) {
        closesocket(nodeSock);
        return "ERROR: Failed to send request";
    }
    
    // Read file size
    char sizeBuf[16] = {0};
    int recvSize = recv(nodeSock, sizeBuf, sizeof(sizeBuf) - 1, 0);
    if (recvSize <= 0) {
        closesocket(nodeSock);
        return "ERROR: Failed to receive file size";
    }
    sizeBuf[recvSize] = '\0';
    int fileSize = atoi(sizeBuf);
    
    if (fileSize <= 0) {
        closesocket(nodeSock);
        return "ERROR: Invalid file size";
    }
    
    // Read checksum
    char checksumBuf[32] = {0};
    int recvChecksum = recv(nodeSock, checksumBuf, sizeof(checksumBuf) - 1, 0);
    if (recvChecksum <= 0) {
        closesocket(nodeSock);
        return "ERROR: Failed to receive checksum";
    }
    checksumBuf[recvChecksum] = '\0';
    unsigned long receivedChecksum = strtoul(checksumBuf, NULL, 10);
    
    // Read file data
    char* fileData = new char[fileSize];
    int totalReceived = 0;
    while (totalReceived < fileSize) {
        int received = recv(nodeSock, fileData + totalReceived, fileSize - totalReceived, 0);
        if (received <= 0) {
            delete[] fileData;
            closesocket(nodeSock);
            return "ERROR: Failed to receive file";
        }
        totalReceived += received;
    }
    closesocket(nodeSock);
    
    // Verify checksum
    unsigned long calculatedChecksum = calculateChecksum(fileData, fileSize);
    if (calculatedChecksum != receivedChecksum) {
        delete[] fileData;
        return "ERROR: Checksum mismatch";
    }
    
    // Send to client
    if (!recoveryMsg.empty()) {
        send(clientSock, recoveryMsg.c_str(), recoveryMsg.size(), 0);
    }
    
    string response = "OK " + to_string(fileSize) + " " + to_string(calculatedChecksum) + "\n";
    if (send(clientSock, response.c_str(), response.size(), 0) <= 0) {
        delete[] fileData;
        return "ERROR: Failed to send response";
    }
    
    int totalSent = 0;
    while (totalSent < fileSize) {
        int sent = send(clientSock, fileData + totalSent, fileSize - totalSent, 0);
        if (sent <= 0) {
            delete[] fileData;
            return "ERROR: Failed to send file";
        }
        totalSent += sent;
    }
    
    cout << "DOWNLOAD: " << dfsPath << " <- Node " << nodeToUse << " (" << fileSize << " bytes)\n";
    
    delete[] fileData;
    return "SUCCESS";
}

string handleList() {
    string result;
    for (auto& pair : fileTable) {
        result += pair.first + "\n";
    }
    if (result.empty()) {
        result = "No files stored\n";
    }
    return result;
}

string handleDelete(const string& dfsPath) {
    updateNodeStatus();
    
    if (fileTable.find(dfsPath) == fileTable.end()) {
        return "ERROR: File not found";
    }
    
    FileEntry entry = fileTable[dfsPath];
    
    // Delete from all nodes
    int deletedCount = 0;
    for (int nodeId : entry.nodeIds) {
        if (nodeAlive[nodeId] && deleteFileFromNode(nodeId, dfsPath)) {
            deletedCount++;
        }
    }
    
    if (deletedCount == 0) {
        return "ERROR: Failed to delete from any node";
    }
    
    fileTable.erase(dfsPath);
    cout << "DELETE: " << dfsPath << " (from " << deletedCount << " node(s))\n";
    return "DELETED";
}

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    
    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(COORDINATOR_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    bind(server, (sockaddr*)&addr, sizeof(addr));
    listen(server, 10);
    
    cout << "Coordinator running on port " << COORDINATOR_PORT << "\n";
    
    while (true) {
        SOCKET client = accept(server, NULL, NULL);
        if (client == INVALID_SOCKET) {
            Sleep(10);
            continue;
        }
        
        opt = 1;
        setsockopt(client, SOL_SOCKET, SO_KEEPALIVE, (char*)&opt, sizeof(opt));
        
        char buffer[4096] = {0};
        int received = recv(client, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) {
            closesocket(client);
            continue;
        }
        
        buffer[received] = '\0';
        string cmd(buffer);
        string response;
        
        try {
            if (cmd.find("REGISTER") == 0) {
                response = handleRegister(cmd);
                send(client, response.c_str(), response.size(), 0);
            }
            else if (cmd.find("UPLOAD") == 0) {
                stringstream ss(cmd);
                string upload, dfsPath;
                ss >> upload >> dfsPath;
                size_t cmdEnd = cmd.find('\n');
                string remainingData = (cmdEnd != string::npos) ? cmd.substr(cmdEnd + 1) : "";
                response = handleUpload(client, dfsPath, remainingData);
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
            else if (cmd.find("DELETE") == 0) {
                stringstream ss(cmd);
                string deleteCmd, dfsPath;
                ss >> deleteCmd >> dfsPath;
                response = handleDelete(dfsPath);
                send(client, response.c_str(), response.size(), 0);
            }
            else {
                response = "ERROR: Unknown command";
                send(client, response.c_str(), response.size(), 0);
            }
        } catch (const exception& e) {
            cerr << "Error: " << e.what() << "\n";
            response = "ERROR: Internal error";
            send(client, response.c_str(), response.size(), 0);
        }
        
        closesocket(client);
    }
    
    closesocket(server);
    WSACleanup();
    return 0;
}
