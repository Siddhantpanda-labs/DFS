#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <windows.h>
#include <filesystem>
#include <direct.h>

#pragma comment(lib, "ws2_32.lib")

using namespace std;
namespace fs = std::filesystem;

const int COORDINATOR_PORT = 9000;
const int NODE_BASE_PORT = 9001;

string storageFolder;
int nodeId;

unsigned long calculateChecksum(const char* data, int size) {
    unsigned long sum = 0;
    for (int i = 0; i < size; i++) {
        sum += (unsigned char)data[i];
    }
    return sum;
}

// Forward declarations
void handleDelete(SOCKET clientSock, const string& dfsPath);

bool registerWithCoordinator() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return false;
    
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

void handleStore(SOCKET clientSock, const string& dfsPath, int fileSize, unsigned long expectedChecksum) {
    cout << "[NODE " << nodeId << "] STORE: " << dfsPath << " (" << fileSize << " bytes)\n";
    
    char* fileData = new char[fileSize];
    int totalReceived = 0;
    
    while (totalReceived < fileSize) {
        int received = recv(clientSock, fileData + totalReceived, fileSize - totalReceived, 0);
        if (received <= 0) {
            cerr << "[NODE " << nodeId << "] Failed to receive file data\n";
            delete[] fileData;
            send(clientSock, "ERROR\n", 6, 0);
            return;
        }
        totalReceived += received;
    }
    
    cout << "[NODE " << nodeId << "] Received " << totalReceived << " bytes\n";
    
    unsigned long calculatedChecksum = calculateChecksum(fileData, fileSize);
    cout << "[NODE " << nodeId << "] Checksum: " << calculatedChecksum << " (expected: " << expectedChecksum << ")\n";
    
    if (calculatedChecksum != expectedChecksum) {
        cerr << "[NODE " << nodeId << "] Checksum mismatch!\n";
        delete[] fileData;
        send(clientSock, "ERROR\n", 6, 0);
        return;
    }
    
    // Strip leading slash from dfsPath if present (Windows treats / as absolute)
    string cleanPath = dfsPath;
    if (!cleanPath.empty() && cleanPath[0] == '/') {
        cleanPath = cleanPath.substr(1);  // Remove leading /
    }
    
    // Replace forward slashes with backslashes for Windows
    for (size_t i = 0; i < cleanPath.length(); i++) {
        if (cleanPath[i] == '/') {
            cleanPath[i] = '\\';
        }
    }
    
    // Create full path using storage folder + cleaned path
    fs::path filePath = fs::path(storageFolder) / cleanPath;
    fs::path absPath = fs::absolute(filePath);
    
    cout << "[NODE " << nodeId << "] Storage folder: " << storageFolder << "\n";
    cout << "[NODE " << nodeId << "] Cleaned path: " << cleanPath << "\n";
    cout << "[NODE " << nodeId << "] Full path: " << absPath << "\n";
    
    // Create parent directories if needed
    fs::path parentDir = filePath.parent_path();
    if (!parentDir.empty() && parentDir != "." && parentDir != filePath.root_path()) {
        cout << "[NODE " << nodeId << "] Creating directories: " << parentDir << "\n";
        try {
            fs::create_directories(parentDir);
        } catch (const exception& e) {
            cerr << "[NODE " << nodeId << "] Failed to create directories: " << e.what() << "\n";
        }
    }
    
    cout << "[NODE " << nodeId << "] Opening file for writing...\n";
    ofstream outFile(filePath, ios::binary);
    if (!outFile.is_open()) {
        cerr << "[NODE " << nodeId << "] Cannot create file: " << filePath << "\n";
        delete[] fileData;
        send(clientSock, "ERROR\n", 6, 0);
        return;
    }
    
    cout << "[NODE " << nodeId << "] Writing " << fileSize << " bytes...\n";
    outFile.write(fileData, fileSize);
    outFile.close();
    delete[] fileData;
    
    // Verify file was created and show absolute path
    if (fs::exists(filePath)) {
        auto file_size = fs::file_size(filePath);
        cout << "[NODE " << nodeId << "] SUCCESS: File saved at " << filePath << " (" << file_size << " bytes)\n";
        cout << "[NODE " << nodeId << "] Absolute path: " << fs::absolute(filePath) << "\n";
    } else {
        cerr << "[NODE " << nodeId << "] ERROR: File was not created at " << filePath << "\n";
        cerr << "[NODE " << nodeId << "] Current directory: " << fs::current_path() << "\n";
    }
    
    send(clientSock, "OK\n", 3, 0);
}

void handleGet(SOCKET clientSock, const string& dfsPath) {
    cout << "[NODE " << nodeId << "] GET request for: " << dfsPath << "\n";
    
    // Strip leading slash and convert to Windows path (same as STORE)
    string cleanPath = dfsPath;
    if (!cleanPath.empty() && cleanPath[0] == '/') {
        cleanPath = cleanPath.substr(1);
    }
    for (size_t i = 0; i < cleanPath.length(); i++) {
        if (cleanPath[i] == '/') {
            cleanPath[i] = '\\';
        }
    }
    
    fs::path filePath = fs::path(storageFolder) / cleanPath;
    fs::path absPath = fs::absolute(filePath);
    
    cout << "[NODE " << nodeId << "] Looking for file at: " << absPath << "\n";
    
    if (!fs::exists(filePath)) {
        cerr << "[NODE " << nodeId << "] File not found: " << absPath << "\n";
        send(clientSock, "ERROR\n", 6, 0);
        return;
    }
    
    ifstream inFile(filePath, ios::binary | ios::ate);
    if (!inFile.is_open()) {
        cerr << "[NODE " << nodeId << "] Cannot open file: " << absPath << "\n";
        send(clientSock, "ERROR\n", 6, 0);
        return;
    }
    
    int fileSize = (int)inFile.tellg();
    cout << "[NODE " << nodeId << "] File size: " << fileSize << " bytes\n";
    inFile.seekg(0, ios::beg);
    
    char* fileData = new char[fileSize];
    inFile.read(fileData, fileSize);
    inFile.close();
    
    unsigned long checksum = calculateChecksum(fileData, fileSize);
    cout << "[NODE " << nodeId << "] Checksum: " << checksum << "\n";
    
    string sizeStr = to_string(fileSize) + "\n";
    send(clientSock, sizeStr.c_str(), sizeStr.size(), 0);
    
    string checksumStr = to_string(checksum) + "\n";
    send(clientSock, checksumStr.c_str(), checksumStr.size(), 0);
    
    cout << "[NODE " << nodeId << "] Sending " << fileSize << " bytes...\n";
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
    cout << "[NODE " << nodeId << "] Sent: " << dfsPath << " (" << fileSize << " bytes)\n";
}

void handleDelete(SOCKET clientSock, const string& dfsPath) {
    // Strip leading slash and convert to Windows path
    string cleanPath = dfsPath;
    if (!cleanPath.empty() && cleanPath[0] == '/') {
        cleanPath = cleanPath.substr(1);
    }
    for (size_t i = 0; i < cleanPath.length(); i++) {
        if (cleanPath[i] == '/') {
            cleanPath[i] = '\\';
        }
    }
    
    fs::path filePath = fs::path(storageFolder) / cleanPath;
    
    if (fs::exists(filePath)) {
        try {
            fs::remove(filePath);
            cout << "[NODE " << nodeId << "] Deleted: " << filePath << "\n";
            send(clientSock, "OK\n", 3, 0);
        } catch (const exception& e) {
            cerr << "[NODE " << nodeId << "] Failed to delete: " << e.what() << "\n";
            send(clientSock, "ERROR\n", 6, 0);
        }
    } else {
        cout << "[NODE " << nodeId << "] File not found: " << filePath << "\n";
        send(clientSock, "OK\n", 3, 0);  // Already deleted or doesn't exist
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
    
    // Use relative path from where executable is run
    storageFolder = "storage\\node" + to_string(nodeId);
    fs::create_directories(storageFolder);
    
    // Convert to absolute path for display
    fs::path absPath = fs::absolute(storageFolder);
    cout << "Node " << nodeId << " storage folder: " << absPath << "\n";
    
    cout << "Registering with coordinator...\n";
    if (!registerWithCoordinator()) {
        cerr << "Failed to register\n";
        return 1;
    }
    
    cout << "Node " << nodeId << " registered successfully\n";
    
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    
    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(NODE_BASE_PORT + nodeId);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    bind(server, (sockaddr*)&addr, sizeof(addr));
    listen(server, 5);
    
    cout << "Node " << nodeId << " listening on port " << (NODE_BASE_PORT + nodeId) << "\n";
    
    while (true) {
        SOCKET client = accept(server, NULL, NULL);
        if (client == INVALID_SOCKET) continue;
        
        char buffer[1024] = {0};
        int received = recv(client, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) {
            closesocket(client);
            continue;
        }
        
        buffer[received] = '\0';
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
