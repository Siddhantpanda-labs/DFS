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

struct FileEntry {
    string filename;
    int node1;
    int node2;
    unsigned long checksum;
};

map<string, FileEntry> fileTable;
map<int, DWORD> nodePids;
map<int, bool> nodeAlive;

const int COORDINATOR_PORT = 9000;
const int NODE_BASE_PORT = 9001;

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

bool sendFileToNode(int nodeId, const string& dfsPath, const char* data, int size, unsigned long checksum) {
    cout << "sendFileToNode: Connecting to Node " << nodeId << " on port " << (NODE_BASE_PORT + nodeId) << "\n";
    
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        cout << "sendFileToNode: Socket creation failed\n";
        return false;
    }
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(NODE_BASE_PORT + nodeId);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        int error = WSAGetLastError();
        cout << "sendFileToNode: Connect failed to Node " << nodeId << " (error: " << error << ")\n";
        closesocket(sock);
        return false;
    }
    
    cout << "sendFileToNode: Connected to Node " << nodeId << "\n";
    
    string cmd = "STORE " + dfsPath + " " + to_string(size) + " " + to_string(checksum) + "\n";
    cout << "sendFileToNode: Sending STORE command to Node " << nodeId << ": " << cmd;
    int sent = send(sock, cmd.c_str(), cmd.size(), 0);
    if (sent <= 0) {
        cout << "sendFileToNode: Failed to send command\n";
        closesocket(sock);
        return false;
    }
    
    cout << "sendFileToNode: Sending " << size << " bytes to Node " << nodeId << "\n";
    int totalSent = 0;
    while (totalSent < size) {
        int sent = send(sock, data + totalSent, size - totalSent, 0);
        if (sent <= 0) {
            cout << "sendFileToNode: Failed to send data\n";
            closesocket(sock);
            return false;
        }
        totalSent += sent;
    }
    
    cout << "sendFileToNode: Sent " << totalSent << " bytes, waiting for response from Node " << nodeId << "\n";
    char response[256] = {0};
    int received = recv(sock, response, sizeof(response) - 1, 0);
    if (received <= 0) {
        cout << "sendFileToNode: No response from Node " << nodeId << "\n";
        closesocket(sock);
        return false;
    }
    
    response[received] = '\0';
    cout << "sendFileToNode: Node " << nodeId << " response: " << response;
    closesocket(sock);
    
    bool success = string(response).find("OK") != string::npos;
    cout << "sendFileToNode: Node " << nodeId << " result: " << (success ? "SUCCESS" : "FAILED") << "\n";
    return success;
}

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
    
    if (availableNodes.size() < 2) {
        return "ERROR: Need at least 2 nodes (found " + to_string(availableNodes.size()) + ")";
    }
    
    // Parse file size from remaining data
    size_t sizeEnd = remainingData.find('\n');
    if (sizeEnd == string::npos) {
        return "ERROR: Invalid format - no size";
    }
    
    string sizeStr = remainingData.substr(0, sizeEnd);
    int fileSize = atoi(sizeStr.c_str());
    
    if (fileSize <= 0 || fileSize > 10 * 1024 * 1024) {
        return "ERROR: Invalid file size";
    }
    
    // Get file data (already in buffer)
    size_t dataStart = sizeEnd + 1;
    int alreadyReceived = remainingData.length() - dataStart;
    
    char* fileData = new char[fileSize];
    if (alreadyReceived > 0) {
        memcpy(fileData, remainingData.c_str() + dataStart, min(alreadyReceived, fileSize));
    }
    
    // Receive remaining file data if needed
    int totalReceived = alreadyReceived;
    while (totalReceived < fileSize) {
        int received = recv(clientSock, fileData + totalReceived, fileSize - totalReceived, 0);
        if (received <= 0) {
            delete[] fileData;
            return "ERROR: Failed to receive file";
        }
        totalReceived += received;
    }
    
    cout << "handleUpload: Received " << totalReceived << " bytes, calculating checksum...\n";
    unsigned long checksum = calculateChecksum(fileData, fileSize);
    int node1 = availableNodes[0];
    int node2 = availableNodes[1];
    
    cout << "handleUpload: Sending to Node " << node1 << " and Node " << node2 << "\n";
    bool success1 = sendFileToNode(node1, dfsPath, fileData, fileSize, checksum);
    cout << "handleUpload: Node " << node1 << " result: " << (success1 ? "OK" : "FAILED") << "\n";
    bool success2 = sendFileToNode(node2, dfsPath, fileData, fileSize, checksum);
    cout << "handleUpload: Node " << node2 << " result: " << (success2 ? "OK" : "FAILED") << "\n";
    
    delete[] fileData;
    
    if (!success1 || !success2) {
        return "ERROR: Failed to store on nodes";
    }
    
    FileEntry entry;
    entry.filename = dfsPath;
    entry.node1 = node1;
    entry.node2 = node2;
    entry.checksum = checksum;
    fileTable[dfsPath] = entry;
    
    cout << "handleUpload: File stored: " << dfsPath << " on Node " << node1 << " and Node " << node2 << "\n";
    string result = "STORED " + to_string(node1) + " " + to_string(node2);
    cout << "handleUpload: Returning: " << result << "\n";
    return result;
}

string handleDownload(SOCKET clientSock, const string& dfsPath) {
    updateNodeStatus();
    
    if (fileTable.find(dfsPath) == fileTable.end()) {
        return "ERROR: File not found";
    }
    
    FileEntry entry = fileTable[dfsPath];
    bool node1Alive = nodeAlive[entry.node1];
    bool node2Alive = nodeAlive[entry.node2];
    
    cout << "Download: Node " << entry.node1 << " alive=" << node1Alive 
         << ", Node " << entry.node2 << " alive=" << node2Alive << "\n";
    
    int nodeToUse = -1;
    string recoveryMsg = "";
    
    if (node1Alive) {
        nodeToUse = entry.node1;
        cout << "Download: Using Node " << nodeToUse << " (primary)\n";
        cout.flush();
    } else if (node2Alive) {
        nodeToUse = entry.node2;
        recoveryMsg = "Node " + to_string(entry.node1) + " failed, recovered using replica on Node " + to_string(nodeToUse) + "\n";
        cout << "Download: FAULT TOLERANCE - Node " << entry.node1 << " is dead, using replica on Node " << nodeToUse << "\n";
        cout.flush();
    } else {
        cout << "Download: ERROR - Both nodes are down!\n";
        cout.flush();
        return "ERROR: Both nodes down";
    }
    
    cout << "Download: Connecting to Node " << nodeToUse << " on port " << (NODE_BASE_PORT + nodeToUse) << "\n";
    cout.flush();
    
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    
    SOCKET nodeSock = socket(AF_INET, SOCK_STREAM, 0);
    if (nodeSock == INVALID_SOCKET) {
        cout << "Download: Failed to create socket\n";
        return "ERROR: Cannot create socket";
    }
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(NODE_BASE_PORT + nodeToUse);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(nodeSock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        int error = WSAGetLastError();
        cout << "Download: Failed to connect to Node " << nodeToUse << " (error: " << error << ")\n";
        closesocket(nodeSock);
        WSACleanup();
        return "ERROR: Cannot connect to node";
    }
    
    cout << "Download: Connected to Node " << nodeToUse << ", sending GET command\n";
    string cmd = "GET " + dfsPath + "\n";
    send(nodeSock, cmd.c_str(), cmd.size(), 0);
    
    cout << "Download: Waiting for file size from Node " << nodeToUse << "\n";
    char sizeBuf[16] = {0};
    int recvSize = recv(nodeSock, sizeBuf, sizeof(sizeBuf) - 1, 0);
    if (recvSize <= 0) {
        cout << "Download: Failed to receive file size\n";
        closesocket(nodeSock);
        return "ERROR: Failed to receive file size";
    }
    sizeBuf[recvSize] = '\0';
    int fileSize = atoi(sizeBuf);
    cout << "Download: File size: " << fileSize << "\n";
    
    if (fileSize <= 0) {
        cout << "Download: Invalid file size\n";
        closesocket(nodeSock);
        WSACleanup();
        return "ERROR: Invalid file size";
    }
    
    cout << "Download: Waiting for checksum from Node " << nodeToUse << "\n";
    char checksumBuf[32] = {0};
    int recvChecksum = recv(nodeSock, checksumBuf, sizeof(checksumBuf) - 1, 0);
    if (recvChecksum <= 0) {
        cout << "Download: Failed to receive checksum\n";
        closesocket(nodeSock);
        return "ERROR: Failed to receive checksum";
    }
    checksumBuf[recvChecksum] = '\0';
    unsigned long receivedChecksum = strtoul(checksumBuf, NULL, 10);
    cout << "Download: Checksum: " << receivedChecksum << "\n";
    
    cout << "Download: Receiving file data (" << fileSize << " bytes) from Node " << nodeToUse << "\n";
    char* fileData = new char[fileSize];
    int totalReceived = 0;
    while (totalReceived < fileSize) {
        int received = recv(nodeSock, fileData + totalReceived, fileSize - totalReceived, 0);
        if (received <= 0) {
            cout << "Download: Failed to receive file data (received " << totalReceived << " of " << fileSize << ")\n";
            delete[] fileData;
            closesocket(nodeSock);
            WSACleanup();
            return "ERROR: Failed to receive file";
        }
        totalReceived += received;
    }
    cout << "Download: Received " << totalReceived << " bytes from Node " << nodeToUse << "\n";
    closesocket(nodeSock);
    WSACleanup();
    
    cout << "Download: Verifying checksum\n";
    unsigned long calculatedChecksum = calculateChecksum(fileData, fileSize);
    if (calculatedChecksum != receivedChecksum) {
        cout << "Download: Checksum mismatch (calculated: " << calculatedChecksum << ", received: " << receivedChecksum << ")\n";
        delete[] fileData;
        return "ERROR: Checksum mismatch";
    }
    
    // Send recovery message first if any, then OK line
    if (!recoveryMsg.empty()) {
        cout << "Download: Sending recovery message\n";
        send(clientSock, recoveryMsg.c_str(), recoveryMsg.size(), 0);
    }
    string response = "OK " + to_string(fileSize) + " " + to_string(calculatedChecksum) + "\n";
    cout << "Download: Sending OK line: " << response;
    send(clientSock, response.c_str(), response.size(), 0);
    
    cout << "Download: Sending file data (" << fileSize << " bytes) to client\n";
    int totalSent = 0;
    while (totalSent < fileSize) {
        int sent = send(clientSock, fileData + totalSent, fileSize - totalSent, 0);
        if (sent <= 0) {
            cout << "Download: Failed to send file data\n";
            delete[] fileData;
            return "ERROR: Failed to send file";
        }
        totalSent += sent;
    }
    cout << "Download: Sent " << totalSent << " bytes to client\n";
    
    delete[] fileData;
    return "SUCCESS";
}

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

// Forward declarations
bool deleteFileFromNode(int nodeId, const string& dfsPath);
string handleDelete(const string& dfsPath);

bool deleteFileFromNode(int nodeId, const string& dfsPath) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return false;
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(NODE_BASE_PORT + nodeId);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(sock);
        return false;
    }
    
    string cmd = "DELETE " + dfsPath + "\n";
    send(sock, cmd.c_str(), cmd.size(), 0);
    
    char response[256] = {0};
    recv(sock, response, sizeof(response), 0);
    closesocket(sock);
    
    return string(response).find("OK") != string::npos;
}

string handleDelete(const string& dfsPath) {
    updateNodeStatus();
    
    if (fileTable.find(dfsPath) == fileTable.end()) {
        return "ERROR: File not found";
    }
    
    FileEntry entry = fileTable[dfsPath];
    
    // Try to delete from both nodes
    bool node1Deleted = false;
    bool node2Deleted = false;
    
    if (nodeAlive[entry.node1]) {
        node1Deleted = deleteFileFromNode(entry.node1, dfsPath);
        if (node1Deleted) {
            cout << "Deleted from Node " << entry.node1 << "\n";
        }
    }
    
    if (nodeAlive[entry.node2]) {
        node2Deleted = deleteFileFromNode(entry.node2, dfsPath);
        if (node2Deleted) {
            cout << "Deleted from Node " << entry.node2 << "\n";
        }
    }
    
    if (!node1Deleted && !node2Deleted) {
        return "ERROR: Failed to delete from both nodes";
    }
    
    // Remove from metadata
    fileTable.erase(dfsPath);
    
    cout << "File deleted: " << dfsPath << "\n";
    return "DELETED";
}

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    
    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(COORDINATOR_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    bind(server, (sockaddr*)&addr, sizeof(addr));
    listen(server, 5);
    
    cout << "Coordinator running on port " << COORDINATOR_PORT << "\n";
    
    while (true) {
        SOCKET client = accept(server, NULL, NULL);
        if (client == INVALID_SOCKET) {
            int error = WSAGetLastError();
            cerr << "Accept failed (error: " << error << ")\n";
            Sleep(100);  // Small delay to avoid busy loop
            continue;
        }
        
        char buffer[1024] = {0};
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
            cerr << "Error processing command: " << e.what() << "\n";
            response = "ERROR: Internal error";
            send(client, response.c_str(), response.size(), 0);
        }
        
        closesocket(client);
    }
    
    closesocket(server);
    WSACleanup();
    return 0;
}
