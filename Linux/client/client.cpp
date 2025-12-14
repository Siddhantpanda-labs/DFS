#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <filesystem>

using namespace std;
namespace fs = std::filesystem;

const int COORDINATOR_PORT = 9000;

// Connect to coordinator
int connectToCoordinator() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        return -1;
    }
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(COORDINATOR_PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        close(sock);
        return -1;
    }
    
    return sock;
}

// Upload file
void uploadFile(const string& localPath, const string& dfsPath) {
    if (!fs::exists(localPath)) {
        cerr << "Error: Local file not found: " << localPath << "\n";
        return;
    }
    
    // Read local file
    ifstream inFile(localPath, ios::binary | ios::ate);
    if (!inFile.is_open()) {
        cerr << "Error: Cannot read file: " << localPath << "\n";
        return;
    }
    
    int fileSize = (int)inFile.tellg();
    inFile.seekg(0, ios::beg);
    
    char* fileData = new char[fileSize];
    inFile.read(fileData, fileSize);
    inFile.close();
    
    // Connect to coordinator
    int sock = connectToCoordinator();
    if (sock == -1) {
        cerr << "Error: Cannot connect to coordinator\n";
        delete[] fileData;
        return;
    }
    
    // Send UPLOAD command
    string cmd = "UPLOAD " + dfsPath + "\n";
    send(sock, cmd.c_str(), cmd.size(), 0);
    
    // Send file size
    string sizeStr = to_string(fileSize) + "\n";
    send(sock, sizeStr.c_str(), sizeStr.size(), 0);
    
    // Send file data
    int totalSent = 0;
    while (totalSent < fileSize) {
        int sent = send(sock, fileData + totalSent, fileSize - totalSent, 0);
        if (sent <= 0) {
            cerr << "Error: Failed to send file data\n";
            delete[] fileData;
            close(sock);
            return;
        }
        totalSent += sent;
    }
    
    delete[] fileData;
    
    // Receive response
    char response[1024] = {0};
    recv(sock, response, sizeof(response), 0);
    
    string resp(response);
    if (resp.find("STORED") == 0) {
        cout << "File uploaded successfully: " << dfsPath << "\n";
        cout << resp << "\n";
    } else {
        cerr << "Upload failed: " << resp << "\n";
    }
    
    close(sock);
}

// Download file
void downloadFile(const string& dfsPath, const string& localPath) {
    int sock = connectToCoordinator();
    if (sock == -1) {
        cerr << "Error: Cannot connect to coordinator\n";
        return;
    }
    
    // Send DOWNLOAD command
    string cmd = "DOWNLOAD " + dfsPath + "\n";
    send(sock, cmd.c_str(), cmd.size(), 0);
    
    // Receive response header
    char header[1024] = {0};
    recv(sock, header, sizeof(header), 0);
    
    string headerStr(header);
    
    // Check for recovery message
    if (headerStr.find("failed") != string::npos || headerStr.find("recovered") != string::npos) {
        cout << headerStr << "\n";
        // Read next line for OK message
        recv(sock, header, sizeof(header), 0);
        headerStr = string(header);
    }
    
    if (headerStr.find("ERROR") == 0) {
        cerr << "Download failed: " << headerStr << "\n";
        close(sock);
        return;
    }
    
    // Parse OK message: "OK <size> <checksum>"
    stringstream ss(headerStr);
    string ok, sizeStr, checksumStr;
    ss >> ok >> sizeStr >> checksumStr;
    
    if (ok != "OK") {
        cerr << "Download failed: Invalid response\n";
        close(sock);
        return;
    }
    
    int fileSize = atoi(sizeStr.c_str());
    unsigned long expectedChecksum = strtoul(checksumStr.c_str(), NULL, 10);
    
    // Receive file data
    char* fileData = new char[fileSize];
    int totalReceived = 0;
    
    while (totalReceived < fileSize) {
        int received = recv(sock, fileData + totalReceived, fileSize - totalReceived, 0);
        if (received <= 0) {
            cerr << "Error: Failed to receive file data\n";
            delete[] fileData;
            close(sock);
            return;
        }
        totalReceived += received;
    }
    
    close(sock);
    
    // Verify checksum
    unsigned long calculatedChecksum = 0;
    for (int i = 0; i < fileSize; i++) {
        calculatedChecksum += (unsigned char)fileData[i];
    }
    
    if (calculatedChecksum != expectedChecksum) {
        cerr << "Error: Checksum mismatch - file may be corrupted\n";
        delete[] fileData;
        return;
    }
    
    // Save to local file
    fs::create_directories(fs::path(localPath).parent_path());
    ofstream outFile(localPath, ios::binary);
    if (!outFile.is_open()) {
        cerr << "Error: Cannot create local file: " << localPath << "\n";
        delete[] fileData;
        return;
    }
    
    outFile.write(fileData, fileSize);
    outFile.close();
    delete[] fileData;
    
    cout << "File downloaded successfully: " << localPath << " (" << fileSize << " bytes)\n";
}

// List files
void listFiles() {
    int sock = connectToCoordinator();
    if (sock == -1) {
        cerr << "Error: Cannot connect to coordinator\n";
        return;
    }
    
    // Send LIST command
    string cmd = "LIST\n";
    send(sock, cmd.c_str(), cmd.size(), 0);
    
    // Receive response
    char response[4096] = {0};
    recv(sock, response, sizeof(response), 0);
    
    cout << "Files in DFS:\n";
    cout << response;
    
    close(sock);
}

void printUsage() {
    cout << "Usage:\n";
    cout << "  ./client upload <local_file> <dfs_path>\n";
    cout << "  ./client download <dfs_path> <local_file>\n";
    cout << "  ./client list\n";
    cout << "\nExamples:\n";
    cout << "  ./client upload test.txt /docs/test.txt\n";
    cout << "  ./client download /docs/test.txt output.txt\n";
    cout << "  ./client list\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }
    
    string command = argv[1];
    
    if (command == "upload") {
        if (argc < 4) {
            cerr << "Error: upload requires <local_file> and <dfs_path>\n";
            printUsage();
            return 1;
        }
        uploadFile(argv[2], argv[3]);
    }
    else if (command == "download") {
        if (argc < 4) {
            cerr << "Error: download requires <dfs_path> and <local_file>\n";
            printUsage();
            return 1;
        }
        downloadFile(argv[2], argv[3]);
    }
    else if (command == "list") {
        listFiles();
    }
    else {
        cerr << "Error: Unknown command: " << command << "\n";
        printUsage();
        return 1;
    }
    
    return 0;
}

