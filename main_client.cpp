// main_client.cpp
#include <iostream>
#include <string>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <atomic>


#ifndef _WIN32

#endif


using namespace std;

#define DEFAULT_PORT 8080

// Declare variables directly (no header needed)
SOCKET sock = INVALID_SOCKET;
atomic<bool> running(true);
string username;

// Function to receive messages from server
void receiveMessages() {
    char buffer[1024];
    while (running) {
        int valread = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (valread > 0) {
            buffer[valread] = '\0';
            // Clear current line and display message
            cout << "\r" << string(100, ' ') << "\r";  // Clear line
            cout << buffer << endl;
            cout << "[" << username << "]> " << flush;  // Show username in prompt
        } else if (valread == 0) {
            cout << "\n  Server disconnected." << endl;
            running = false;
            break;
        } else {
            int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK) {
                cout << "\n Connection error: " << error << endl;
                running = false;
                break;
            }
        }
    }
}

int main() {
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "❌ WSAStartup failed." << endl;
        return -1;
    }

    cout << "Enter your username: ";
    getline(cin, username);
    
    string server_host;
    int port;

    cout << "Enter server hostname/IP (e.g. 127.0.0.1): ";
    getline(cin, server_host);
    cout << "Enter server port (e.g. 8080): ";
    cin >> port;
    cin.ignore(); // clear newline from buffer

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        cerr << " Socket creation error: " << WSAGetLastError() << endl;
        WSACleanup();
        return -1;
    }

    // Set up server address
    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_host.c_str(), &serv_addr.sin_addr) <= 0) {
        hostent* he = gethostbyname(server_host.c_str());
        if (!he) {
            cerr << " Host not found: " << server_host << endl;
            closesocket(sock);
            WSACleanup();
            return -1;
        }
        memcpy(&serv_addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    // Connect to server
    if (connect(sock, (sockaddr*)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR) {
        cerr << " Connection failed: " << WSAGetLastError() << endl;
        closesocket(sock);
        WSACleanup();
        return -1;
    }

    // Send username to server immediately
    send(sock, username.c_str(), username.size(), 0);

    cout << "==========================================" << endl;
    cout << "          CHAT APPLICATION COMMANDS       " << endl;
    cout << "==========================================" << endl;
    cout << "/join <room>           - Join or create a chat room" << endl;
    cout << "/pm <user> <message>   - Send private message to a user" << endl;
    cout << "/reply <user> <msg>    - Reply to a user in the current room" << endl;
    cout << "/undo                  - Undo your last message" << endl;
    cout << "/redo                  - Redo your last undone message" << endl;
    cout << "/history               - Show message history for current room" << endl;
    cout << "/search <keyword>      - Search for messages containing keyword" << endl;
    cout << "/quit                  - Exit the chat application" << endl;
    cout << "/help                  - Show this help message" << endl;
    cout << "==========================================" << endl;


    // Make socket non-blocking

    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);

    // Start receiving thread
    thread recvThread(receiveMessages);

    string msg;
    while (running) {
        cout << "[" << username << "]> " << flush;
        getline(cin, msg);

        if (msg == "/quit") {
            running = false;
            break;
        }

        if (send(sock, msg.c_str(), msg.size(), 0) == SOCKET_ERROR) {
            cerr << " Send failed: " << WSAGetLastError() << endl;
            running = false;
            break;
        }

        Sleep(100); // small delay
    }

    // Cleanup
    running = false;
    if (recvThread.joinable()) recvThread.join();
    closesocket(sock);
    WSACleanup();

    cout << username + " Disconnected from server." << endl;
    return 0;
}