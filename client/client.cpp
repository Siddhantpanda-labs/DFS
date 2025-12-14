#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <filesystem>

#pragma comment(lib, "ws2_32.lib")

using namespace std;
namespace fs = std::filesystem;

const int COORDINATOR_PORT = 9000;

SOCKET connectToCoordinator() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return INVALID_SOCKET;
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(COORDINATOR_PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(sock);
        WSACleanup();
        return INVALID_SOCKET;
    }
    
    return sock;
}

void uploadFile(const string& localPath, const string& dfsPath) {
    if (!fs::exists(localPath)) {
        cerr << "File not found: " << localPath << "\n";
        return;
    }
    
    ifstream inFile(localPath, ios::binary | ios::ate);
    if (!inFile.is_open()) {
        cerr << "Cannot read file\n";
        return;
    }
    
    int fileSize = (int)inFile.tellg();
    inFile.seekg(0, ios::beg);
    
    char* fileData = new char[fileSize];
    inFile.read(fileData, fileSize);
    inFile.close();
    
    SOCKET sock = connectToCoordinator();
    if (sock == INVALID_SOCKET) {
        cerr << "Cannot connect to coordinator\n";
        delete[] fileData;
        return;
    }
    
    string cmd = "UPLOAD " + dfsPath + "\n";
    send(sock, cmd.c_str(), cmd.size(), 0);
    
    string sizeStr = to_string(fileSize) + "\n";
    send(sock, sizeStr.c_str(), sizeStr.size(), 0);
    
    int totalSent = 0;
    while (totalSent < fileSize) {
        int sent = send(sock, fileData + totalSent, fileSize - totalSent, 0);
        if (sent <= 0) {
            cerr << "Failed to send file\n";
            delete[] fileData;
            closesocket(sock);
            WSACleanup();
            return;
        }
        totalSent += sent;
    }
    
    delete[] fileData;
    
    cout << "Waiting for response...\n";
    
    char response[1024] = {0};
    int received = recv(sock, response, sizeof(response) - 1, 0);
    
    if (received <= 0) {
        cerr << "No response from coordinator\n";
        closesocket(sock);
        WSACleanup();
        return;
    }
    
    response[received] = '\0';
    string resp(response);
    
    if (resp.find("STORED") == 0) {
        cout << "Upload successful: " << resp;
    } else {
        cerr << "Upload failed: " << resp;
    }
    
    closesocket(sock);
    WSACleanup();
}

void downloadFile(const string& dfsPath, const string& localPath) {
    SOCKET sock = connectToCoordinator();
    if (sock == INVALID_SOCKET) {
        cerr << "Cannot connect to coordinator\n";
        return;
    }
    
    string cmd = "DOWNLOAD " + dfsPath + "\n";
    send(sock, cmd.c_str(), cmd.size(), 0);
    
    // Read first line - could be recovery message or OK line
    char lineBuffer[1024] = {0};
    int linePos = 0;
    string firstLine = "";
    
    // Read first line
    while (linePos < sizeof(lineBuffer) - 1) {
        int received = recv(sock, lineBuffer + linePos, 1, 0);
        if (received <= 0) {
            cerr << "No response from coordinator\n";
            closesocket(sock);
            WSACleanup();
            return;
        }
        if (lineBuffer[linePos] == '\n') {
            lineBuffer[linePos] = '\0';
            firstLine = string(lineBuffer);
            break;
        }
        linePos++;
    }
    
    // Check if first line is recovery message
    if (firstLine.find("failed") != string::npos || firstLine.find("recovered") != string::npos) {
        cout << firstLine << "\n";
        // Read next line which should be OK line
        linePos = 0;
        memset(lineBuffer, 0, sizeof(lineBuffer));
        while (linePos < sizeof(lineBuffer) - 1) {
            int received = recv(sock, lineBuffer + linePos, 1, 0);
            if (received <= 0) {
                cerr << "No response from coordinator\n";
                closesocket(sock);
                WSACleanup();
                return;
            }
            if (lineBuffer[linePos] == '\n') {
                lineBuffer[linePos] = '\0';
                firstLine = string(lineBuffer);
                break;
            }
            linePos++;
        }
    }
    
    if (firstLine.find("ERROR") == 0) {
        cerr << "Download failed: " << firstLine << "\n";
        closesocket(sock);
        WSACleanup();
        return;
    }
    
    // Parse OK line: "OK <size> <checksum>"
    stringstream ss(firstLine);
    string ok, sizeStr, checksumStr;
    ss >> ok >> sizeStr >> checksumStr;
    
    if (ok != "OK") {
        cerr << "Invalid response: " << firstLine << "\n";
        closesocket(sock);
        WSACleanup();
        return;
    }
    
    int fileSize = atoi(sizeStr.c_str());
    unsigned long expectedChecksum = strtoul(checksumStr.c_str(), NULL, 10);
    
    cout << "Receiving file (" << fileSize << " bytes)...\n";
    
    char* fileData = new char[fileSize];
    int fileReceived = 0;
    
    while (fileReceived < fileSize) {
        int received = recv(sock, fileData + fileReceived, fileSize - fileReceived, 0);
        if (received <= 0) {
            cerr << "Failed to receive file (received " << fileReceived << " of " << fileSize << " bytes)\n";
            delete[] fileData;
            closesocket(sock);
            WSACleanup();
            return;
        }
        fileReceived += received;
    }
    
    cout << "Received " << fileReceived << " bytes\n";
    
    closesocket(sock);
    WSACleanup();
    
    unsigned long calculatedChecksum = 0;
    for (int i = 0; i < fileSize; i++) {
        calculatedChecksum += (unsigned char)fileData[i];
    }
    
    if (calculatedChecksum != expectedChecksum) {
        cerr << "Checksum mismatch\n";
        delete[] fileData;
        return;
    }
    
    // Create parent directory if needed
    fs::path localFilePath(localPath);
    fs::path parentDir = localFilePath.parent_path();
    if (!parentDir.empty() && parentDir != "." && parentDir != localFilePath.root_path()) {
        fs::create_directories(parentDir);
    }
    
    ofstream outFile(localPath, ios::binary);
    if (!outFile.is_open()) {
        cerr << "Cannot create file\n";
        delete[] fileData;
        return;
    }
    
    outFile.write(fileData, fileSize);
    outFile.close();
    delete[] fileData;
    
    cout << "Download successful: " << localPath << " (" << fileSize << " bytes)\n";
}

void listFiles() {
    SOCKET sock = connectToCoordinator();
    if (sock == INVALID_SOCKET) {
        cerr << "Cannot connect to coordinator\n";
        return;
    }
    
    string cmd = "LIST\n";
    send(sock, cmd.c_str(), cmd.size(), 0);
    
    char response[4096] = {0};
    recv(sock, response, sizeof(response), 0);
    
    cout << "Files in DFS:\n" << response;
    
    closesocket(sock);
    WSACleanup();
}

void deleteFile(const string& dfsPath) {
    SOCKET sock = connectToCoordinator();
    if (sock == INVALID_SOCKET) {
        cerr << "Cannot connect to coordinator\n";
        return;
    }
    
    string cmd = "DELETE " + dfsPath + "\n";
    send(sock, cmd.c_str(), cmd.size(), 0);
    
    char response[256] = {0};
    recv(sock, response, sizeof(response), 0);
    
    string resp(response);
    if (resp.find("DELETED") != string::npos) {
        cout << "File deleted successfully: " << dfsPath << "\n";
    } else {
        cerr << "Delete failed: " << resp;
    }
    
    closesocket(sock);
    WSACleanup();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cout << "Usage: client.exe upload <local> <dfs>\n";
        cout << "       client.exe download <dfs> <local>\n";
        cout << "       client.exe delete <dfs>\n";
        cout << "       client.exe list\n";
        return 1;
    }
    
    string command = argv[1];
    
    if (command == "upload") {
        if (argc < 4) {
            cerr << "Usage: client.exe upload <local_file> <dfs_path>\n";
            return 1;
        }
        uploadFile(argv[2], argv[3]);
    }
    else if (command == "download") {
        if (argc < 4) {
            cerr << "Usage: client.exe download <dfs_path> <local_file>\n";
            return 1;
        }
        downloadFile(argv[2], argv[3]);
    }
    else if (command == "list") {
        listFiles();
    }
    else if (command == "delete") {
        if (argc < 3) {
            cerr << "Usage: client.exe delete <dfs_path>\n";
            return 1;
        }
        deleteFile(argv[2]);
    }
    else {
        cerr << "Unknown command\n";
        return 1;
    }
    
    return 0;
}
