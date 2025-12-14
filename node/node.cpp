#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <windows.h>
#include <filesystem>

#pragma comment(lib, "ws2_32.lib")

using namespace std;
namespace fs = std::filesystem;

// Constants
const int COORDINATOR_PORT = 9000;
const int NODE_BASE_PORT = 9001;
const DWORD SOCKET_TIMEOUT = 30000;  // 30 seconds for file transfers

// Global state
string storageFolder;
int nodeId;

// Utility functions
unsigned long calculateChecksum(const char* data, int size) {
    unsigned long sum = 0;
    for (int i = 0; i < size; i++) {
        sum += (unsigned char)data[i];
    }
    return sum;
}

string cleanPath(const string& dfsPath) {
    string clean = dfsPath;
    if (!clean.empty() && clean[0] == '/') {
        clean = clean.substr(1);
    }
    for (size_t i = 0; i < clean.length(); i++) {
        if (clean[i] == '/') {
            clean[i] = '\\';
        }
    }
    return clean;
}

fs::path getFilePath(const string& dfsPath) {
    return fs::path(storageFolder) / cleanPath(dfsPath);
}

bool registerWithCoordinator() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(COORDINATOR_PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(sock);
        WSACleanup();
        return false;
    }
    
    DWORD pid = GetCurrentProcessId();
    string cmd = "REGISTER " + to_string(nodeId) + " " + to_string(pid) + "\n";
    send(sock, cmd.c_str(), cmd.size(), 0);
    
    char response[256] = {0};
    recv(sock, response, sizeof(response), 0);
    
    closesocket(sock);
    WSACleanup();
    
    return string(response).find("REGISTERED") != string::npos;
}

// Command handlers
void handleStore(SOCKET clientSock, const string& dfsPath, int fileSize, unsigned long expectedChecksum, const string& remainingData) {
    // Set timeout for receiving file data
    DWORD timeout = SOCKET_TIMEOUT;
    setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    
    // Receive file data
    char* fileData = new char[fileSize];
    int totalReceived = 0;
    
    // Copy any data already in buffer
    if (!remainingData.empty()) {
        int toCopy = min((int)remainingData.length(), fileSize);
        memcpy(fileData, remainingData.c_str(), toCopy);
        totalReceived = toCopy;
    }
    
    // Receive remaining data
    while (totalReceived < fileSize) {
        int received = recv(clientSock, fileData + totalReceived, fileSize - totalReceived, 0);
        if (received <= 0) {
            cerr << "[NODE " << nodeId << "] Failed to receive file data (received " << totalReceived << " of " << fileSize << " bytes)\n";
            delete[] fileData;
            send(clientSock, "ERROR\n", 6, 0);
            return;
        }
        totalReceived += received;
    }
    
    // Verify checksum
    unsigned long calculatedChecksum = calculateChecksum(fileData, fileSize);
    if (calculatedChecksum != expectedChecksum) {
        cerr << "[NODE " << nodeId << "] Checksum mismatch\n";
        delete[] fileData;
        send(clientSock, "ERROR\n", 6, 0);
        return;
    }
    
    // Save file
    fs::path filePath = getFilePath(dfsPath);
    fs::path parentDir = filePath.parent_path();
    
    if (!parentDir.empty() && parentDir != "." && parentDir != filePath.root_path()) {
        try {
            fs::create_directories(parentDir);
        } catch (const exception& e) {
            cerr << "[NODE " << nodeId << "] Failed to create directories: " << e.what() << "\n";
            delete[] fileData;
            send(clientSock, "ERROR\n", 6, 0);
            return;
        }
    }
    
    ofstream outFile(filePath, ios::binary);
    if (!outFile.is_open()) {
        cerr << "[NODE " << nodeId << "] Cannot create file: " << filePath << "\n";
        delete[] fileData;
        send(clientSock, "ERROR\n", 6, 0);
        return;
    }
    
    outFile.write(fileData, fileSize);
    outFile.close();
    delete[] fileData;
    
    if (fs::exists(filePath)) {
        cout << "[NODE " << nodeId << "] STORE: " << dfsPath << " (" << fileSize << " bytes)\n";
        send(clientSock, "OK\n", 3, 0);
    } else {
        cerr << "[NODE " << nodeId << "] ERROR: File was not created\n";
        send(clientSock, "ERROR\n", 6, 0);
    }
}

void handleGet(SOCKET clientSock, const string& dfsPath) {
    fs::path filePath = getFilePath(dfsPath);
    
    if (!fs::exists(filePath)) {
        cerr << "[NODE " << nodeId << "] File not found: " << dfsPath << "\n";
        send(clientSock, "ERROR\n", 6, 0);
        return;
    }
    
    ifstream inFile(filePath, ios::binary | ios::ate);
    if (!inFile.is_open()) {
        cerr << "[NODE " << nodeId << "] Cannot open file: " << dfsPath << "\n";
        send(clientSock, "ERROR\n", 6, 0);
        return;
    }
    
    int fileSize = (int)inFile.tellg();
    inFile.seekg(0, ios::beg);
    
    char* fileData = new char[fileSize];
    inFile.read(fileData, fileSize);
    inFile.close();
    
    unsigned long checksum = calculateChecksum(fileData, fileSize);
    
    // Send size, checksum, then data
    string sizeStr = to_string(fileSize) + "\n";
    send(clientSock, sizeStr.c_str(), sizeStr.size(), 0);
    
    string checksumStr = to_string(checksum) + "\n";
    send(clientSock, checksumStr.c_str(), checksumStr.size(), 0);
    
    int totalSent = 0;
    while (totalSent < fileSize) {
        int sent = send(clientSock, fileData + totalSent, fileSize - totalSent, 0);
        if (sent <= 0) {
            delete[] fileData;
            return;
        }
        totalSent += sent;
    }
    
    delete[] fileData;
    cout << "[NODE " << nodeId << "] GET: " << dfsPath << " (" << fileSize << " bytes)\n";
}

void handleDelete(SOCKET clientSock, const string& dfsPath) {
    fs::path filePath = getFilePath(dfsPath);
    
    if (fs::exists(filePath)) {
        try {
            fs::remove(filePath);
            cout << "[NODE " << nodeId << "] DELETE: " << dfsPath << "\n";
            send(clientSock, "OK\n", 3, 0);
        } catch (const exception& e) {
            cerr << "[NODE " << nodeId << "] Failed to delete: " << e.what() << "\n";
            send(clientSock, "ERROR\n", 6, 0);
        }
    } else {
        send(clientSock, "OK\n", 3, 0);
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: node.exe <nodeId>\n";
        return 1;
    }
    
    nodeId = atoi(argv[1]);
    if (nodeId < 1) {
        cerr << "Invalid node ID\n";
        return 1;
    }
    
    storageFolder = "storage\\node" + to_string(nodeId);
    fs::create_directories(storageFolder);
    
    if (!registerWithCoordinator()) {
        cerr << "Failed to register\n";
        return 1;
    }
    
    cout << "Node " << nodeId << " registered\n";
    
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    
    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(NODE_BASE_PORT + nodeId);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    bind(server, (sockaddr*)&addr, sizeof(addr));
    listen(server, 5);
    
    while (true) {
        SOCKET client = accept(server, NULL, NULL);
        if (client == INVALID_SOCKET) continue;
        
        // Set default timeout for command reading
        DWORD timeout = SOCKET_TIMEOUT;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
        
        char buffer[1024] = {0};
        int received = recv(client, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) {
            closesocket(client);
            continue;
        }
        
        buffer[received] = '\0';
        string cmdLine(buffer);
        size_t newlinePos = cmdLine.find('\n');
        
        if (newlinePos == string::npos) {
            closesocket(client);
            continue;
        }
        
        stringstream ss(cmdLine.substr(0, newlinePos));
        string command;
        ss >> command;
        
        if (command == "STORE") {
            string dfsPath;
            int fileSize;
            unsigned long checksum;
            ss >> dfsPath >> fileSize >> checksum;
            
            // Extract any file data already in buffer (after the command line)
            string remainingData;
            if (received > (int)(newlinePos + 1)) {
                remainingData = string(buffer + newlinePos + 1, received - (newlinePos + 1));
            }
            
            handleStore(client, dfsPath, fileSize, checksum, remainingData);
        }
        else if (command == "GET") {
            string dfsPath;
            ss >> dfsPath;
            handleGet(client, dfsPath);
        }
        else if (command == "DELETE") {
            string dfsPath;
            ss >> dfsPath;
            handleDelete(client, dfsPath);
        }
        
        closesocket(client);
    }
    
    closesocket(server);
    WSACleanup();
    return 0;
}
