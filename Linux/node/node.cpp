#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <sys/stat.h>
#include <filesystem>

using namespace std;
namespace fs = std::filesystem;

const int COORDINATOR_PORT = 9000;
const int NODE_BASE_PORT = 9001;

string storageFolder;
int nodeId;

// Calculate checksum
unsigned long calculateChecksum(const char* data, int size) {
    unsigned long sum = 0;
    for (int i = 0; i < size; i++) {
        sum += (unsigned char)data[i];
    }
    return sum;
}

// Register with coordinator
bool registerWithCoordinator() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        return false;
    }
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(COORDINATOR_PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        close(sock);
        return false;
    }
    
    pid_t pid = getpid();
    string cmd = "REGISTER " + to_string(nodeId) + " " + to_string(pid) + "\n";
    send(sock, cmd.c_str(), cmd.size(), 0);
    
    char response[256] = {0};
    recv(sock, response, sizeof(response), 0);
    
    close(sock);
    return string(response).find("REGISTERED") != string::npos;
}

// Handle STORE command
void handleStore(int clientSock, const string& dfsPath, int fileSize, unsigned long expectedChecksum) {
    // Receive file data
    char* fileData = new char[fileSize];
    int totalReceived = 0;
    
    while (totalReceived < fileSize) {
        int received = recv(clientSock, fileData + totalReceived, fileSize - totalReceived, 0);
        if (received <= 0) {
            delete[] fileData;
            send(clientSock, "ERROR: Failed to receive file\n", 31, 0);
            return;
        }
        totalReceived += received;
    }
    
    // Verify checksum
    unsigned long calculatedChecksum = calculateChecksum(fileData, fileSize);
    if (calculatedChecksum != expectedChecksum) {
        delete[] fileData;
        send(clientSock, "ERROR: Checksum mismatch\n", 25, 0);
        return;
    }
    
    // Save file
    fs::path filePath = fs::path(storageFolder) / dfsPath;
    fs::create_directories(filePath.parent_path());
    
    ofstream outFile(filePath, ios::binary);
    if (!outFile.is_open()) {
        delete[] fileData;
        send(clientSock, "ERROR: Cannot create file\n", 26, 0);
        return;
    }
    
    outFile.write(fileData, fileSize);
    outFile.close();
    delete[] fileData;
    
    send(clientSock, "OK\n", 3, 0);
    cout << "Stored file: " << dfsPath << " (" << fileSize << " bytes)\n";
}

// Handle GET command
void handleGet(int clientSock, const string& dfsPath) {
    fs::path filePath = fs::path(storageFolder) / dfsPath;
    
    if (!fs::exists(filePath)) {
        send(clientSock, "ERROR: File not found\n", 22, 0);
        return;
    }
    
    // Read file
    ifstream inFile(filePath, ios::binary | ios::ate);
    if (!inFile.is_open()) {
        send(clientSock, "ERROR: Cannot read file\n", 24, 0);
        return;
    }
    
    int fileSize = (int)inFile.tellg();
    inFile.seekg(0, ios::beg);
    
    char* fileData = new char[fileSize];
    inFile.read(fileData, fileSize);
    inFile.close();
    
    // Calculate checksum
    unsigned long checksum = calculateChecksum(fileData, fileSize);
    
    // Send file size
    string sizeStr = to_string(fileSize) + "\n";
    send(clientSock, sizeStr.c_str(), sizeStr.size(), 0);
    
    // Send checksum
    string checksumStr = to_string(checksum) + "\n";
    send(clientSock, checksumStr.c_str(), checksumStr.size(), 0);
    
    // Send file data
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
    cout << "Sent file: " << dfsPath << " (" << fileSize << " bytes)\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: ./node <nodeId>\n";
        return 1;
    }
    
    nodeId = atoi(argv[1]);
    if (nodeId < 1) {
        cerr << "Invalid node ID (must be >= 1)\n";
        return 1;
    }
    
    // Check if port would be valid (Linux ports are 1-65535)
    if (NODE_BASE_PORT + nodeId > 65535) {
        cerr << "Invalid node ID (port " << (NODE_BASE_PORT + nodeId) << " exceeds maximum)\n";
        return 1;
    }
    
    storageFolder = "storage/node" + to_string(nodeId);
    fs::create_directories(storageFolder);
    
    // Register with coordinator
    if (!registerWithCoordinator()) {
        cerr << "Failed to register with coordinator\n";
        return 1;
    }
    
    cout << "Node " << nodeId << " registered with coordinator\n";
    
    // Create server socket
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
    addr.sin_port = htons(NODE_BASE_PORT + nodeId);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(server, (sockaddr*)&addr, sizeof(addr)) != 0) {
        cerr << "Bind failed on port " << (NODE_BASE_PORT + nodeId) << "\n";
        close(server);
        return 1;
    }
    
    if (listen(server, 5) != 0) {
        cerr << "Listen failed\n";
        close(server);
        return 1;
    }
    
    cout << "Node " << nodeId << " running on port " << (NODE_BASE_PORT + nodeId) << "...\n";
    cout << "Storage folder: " << storageFolder << "\n";
    
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
        stringstream ss(cmd);
        string command;
        ss >> command;
        
        if (command == "STORE") {
            string dfsPath;
            int fileSize;
            unsigned long checksum;
            ss >> dfsPath >> fileSize >> checksum;
            handleStore(client, dfsPath, fileSize, checksum);
        }
        else if (command == "GET") {
            string dfsPath;
            ss >> dfsPath;
            handleGet(client, dfsPath);
        }
        else {
            send(client, "ERROR: Unknown command\n", 24, 0);
        }
        
        close(client);
    }
    
    close(server);
    return 0;
}

