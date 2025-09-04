// main_server.cpp
#include <iostream>
#include <string>
#include <thread>   //for multiple user
#include <map>
#include <set>
#include <mutex>    // for locking 
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ctime>     // real time 
#include <stack>      // for undo and redo message storage 
#include <queue>
#include <list>
#include <iomanip>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")
using namespace std;

#define PORT 8080
#define MAX_MESSAGE_HISTORY 1000  // Maximum messages to keep in history

// ==========================
// Utility Functions
// ==========================

string getCurrentTimeString() {
    time_t now = time(nullptr);
    tm localTime;
    localtime_s(&localTime, &now);
    
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", &localTime);
    return string(buffer);
}

string formatMessageWithTime(const string& sender, const string& message) {
    return "[" + getCurrentTimeString() + "][" + sender + "]: " + message;
}

// ==========================
// Message Class
// ==========================

class Message {
public:
    int id;
    string sender;
    string text;
    time_t timestamp;

    Message(int i = 0, string s = "", string t = "")
        : id(i), sender(s), text(t) {
        timestamp = time(nullptr);
    }

    string toString() const {
        return formatMessageWithTime(sender, text);
    }
};

// ==========================
// Message Queue for Broadcasting
// ==========================

class MessageQueue {
private:
    queue<Message> messages;
    mutex mtx;
    bool shutdown = false;

public:
    void push(const Message& msg) {
        lock_guard<mutex> lock(mtx);
        messages.push(msg);
    }

    bool pop(Message& msg) {
        lock_guard<mutex> lock(mtx);
        if (messages.empty() || shutdown) return false;
        
        msg = messages.front();
        messages.pop();
        return true;
    }

    void shutdownQueue() {
        lock_guard<mutex> lock(mtx);
        shutdown = true;
    }
};

// ==========================
// History (Linked List based)
// ==========================

class History {
private:
    struct Node {
        Message message;
        Node* next;
        Node* prev;
        
        Node(const Message& msg) : message(msg), next(nullptr), prev(nullptr) {}
    };
    
    Node* head;
    Node* tail;
    int size;
    int maxSize;
    mutable mutex mtx;  // mutable for const methods

public:
    History(int maxSize = MAX_MESSAGE_HISTORY) : head(nullptr), tail(nullptr), size(0), maxSize(maxSize) {}
    
    ~History() {
        clear();
    }
    
    void addMessage(const Message& msg) {
        lock_guard<mutex> lock(mtx);
        
        Node* newNode = new Node(msg);
        
        if (head == nullptr) {
            head = tail = newNode;
        } else {
            tail->next = newNode;
            newNode->prev = tail;
            tail = newNode;
        }
        
        size++;
        
        // Remove oldest message if exceeding max size
        if (size > maxSize) {
            Node* temp = head;
            head = head->next;
            if (head) head->prev = nullptr;
            delete temp;
            size--;
        }
    }
    
    void removeMessage(int messageId) {
        lock_guard<mutex> lock(mtx);
        
        Node* current = head;
        while (current != nullptr) {
            if (current->message.id == messageId) {
                if (current->prev) current->prev->next = current->next;
                if (current->next) current->next->prev = current->prev;
                if (current == head) head = current->next;
                if (current == tail) tail = current->prev;
                
                delete current;
                size--;
                return;
            }
            current = current->next;
        }
    }
    
    list<Message> getMessages() const {
        lock_guard<mutex> lock(mtx);
        list<Message> result;
        
        Node* current = head;
        while (current != nullptr) {
            result.push_back(current->message);
            current = current->next;
        }
        
        return result;
    }
    
    list<Message> searchMessages(const string& keyword) const {
        lock_guard<mutex> lock(mtx);
        list<Message> result;
        
        Node* current = head;
        while (current != nullptr) {
            if (current->message.text.find(keyword) != string::npos) {
                result.push_back(current->message);
            }
            current = current->next;
        }
        
        return result;
    }
    
    void clear() {
        lock_guard<mutex> lock(mtx);
        
        Node* current = head;
        while (current != nullptr) {
            Node* next = current->next;
            delete current;
            current = next;
        }
        
        head = tail = nullptr;
        size = 0;
    }
};

// ==========================
// Undo/Redo
// ==========================

class UndoRedo {
private:
    stack<Message> undoStack;
    stack<Message> redoStack;
    mutex mtx;

public:
    void addMessage(const Message& msg) {
        lock_guard<mutex> lock(mtx);
        undoStack.push(msg);
        while (!redoStack.empty()) 
            redoStack.pop();
    }

    bool undo(Message& msg) {
        lock_guard<mutex> lock(mtx);
        if (undoStack.empty()) return false;
        msg = undoStack.top();
        undoStack.pop();
        redoStack.push(msg);
        return true;
    }

    bool redo(Message& msg) {
        lock_guard<mutex> lock(mtx);
        if (redoStack.empty()) return false;
        msg = redoStack.top();
        redoStack.pop();
        undoStack.push(msg);
        return true;
    }
};

// ==========================
// Server Data
// ==========================

map<SOCKET, string> clients;           // socket -> username
map<string, set<SOCKET>> rooms;        // room -> client sockets
mutex clientsMtx;

History roomHistory;
UndoRedo undoRedo;
MessageQueue messageQueue;
int messageCounter = 0;

// ==========================
// Broadcast Worker Thread
// ==========================

void broadcastWorker() {
    while (true) {
        Message msg;
        if (!messageQueue.pop(msg)) {
            // Small delay to prevent busy waiting
            this_thread::sleep_for(chrono::milliseconds(100));
            continue;
        }
        
        string fullMsg = msg.toString() + "\n";
        string senderTimeMsg = "[" + getCurrentTimeString() + "] "  + "\n";
        
        lock_guard<mutex> lock(clientsMtx);
        
        // Find which room the sender is in
        string targetRoom;
        for (const auto& room : rooms) {
            for (const auto& client : room.second) {
                auto it = clients.find(client);
                if (it != clients.end() && it->second == msg.sender) {
                    targetRoom = room.first;
                    break;
                }
            }
            if (!targetRoom.empty()) break;
        }
        
        if (targetRoom.empty() || rooms.find(targetRoom) == rooms.end()) 
            continue;
            
        for (SOCKET clientSock : rooms[targetRoom]) {
            auto it = clients.find(clientSock);
            if (it != clients.end()) {
                if (it->second == msg.sender) {
                    // Send to sender with "You" prefix and current time
                    send(clientSock, senderTimeMsg.c_str(), (int)senderTimeMsg.size(), 0);
                } else {
                    // Send to receivers with original formatted message
                    send(clientSock, fullMsg.c_str(), (int)fullMsg.size(), 0);
                }
            }
        }
    }
}

// ==========================
// Handle Client
// ==========================

void handleClient(SOCKET clientSock) {
    char buffer[1024];
    string currentRoom = "chatroom";
    string username;

    // Receive username
    int valread = recv(clientSock, buffer, sizeof(buffer) - 1, 0);
    if (valread <= 0) {
        closesocket(clientSock);
        return;
    }
    buffer[valread] = '\0';
    username = string(buffer);

    {
        lock_guard<mutex> lock(clientsMtx);
        clients[clientSock] = username;
        rooms[currentRoom].insert(clientSock);
    }

    string welcome = "[" + getCurrentTimeString() + "] Connected as '" + username + "' to chat server. You are in room: " + currentRoom + "\n";
    send(clientSock, welcome.c_str(), (int)welcome.size(), 0);

    // Notify others in the room
    string joinNotice = "[" + getCurrentTimeString() + "] " + username + " joined the room\n";
    {
        lock_guard<mutex> lock(clientsMtx);
        for (SOCKET otherSock : rooms[currentRoom]) {
            if (otherSock != clientSock) {
                send(otherSock, joinNotice.c_str(), (int)joinNotice.size(), 0);
            }
        }
    }

    while (true) {
        valread = recv(clientSock, buffer, sizeof(buffer) - 1, 0);
        if (valread <= 0) {
            lock_guard<mutex> lock(clientsMtx);
            rooms[currentRoom].erase(clientSock);
            
            // Notify others about user leaving
            string leaveNotice = "[" + getCurrentTimeString() + "] " + username + " left the room\n";
            for (SOCKET otherSock : rooms[currentRoom]) {
                send(otherSock, leaveNotice.c_str(), (int)leaveNotice.size(), 0);
            }
            
            clients.erase(clientSock);
            closesocket(clientSock);
            break;
        }

        buffer[valread] = '\0';
        string msg(buffer);

        // ================= Commands =================
        if (msg.rfind("/join", 0) == 0) {
            string room = msg.substr(6);
            string oldRoom = currentRoom;
            
            {
                lock_guard<mutex> lock(clientsMtx);
                rooms[oldRoom].erase(clientSock);
                
                // Notify old room about leaving
                string leaveNotice = "[" + getCurrentTimeString() + "] " + username + " left the room\n";
                for (SOCKET otherSock : rooms[oldRoom]) {
                    send(otherSock, leaveNotice.c_str(), (int)leaveNotice.size(), 0);
                }
                
                currentRoom = room;
                rooms[currentRoom].insert(clientSock);
                
                // Notify new room about joining
                string joinNotice = "[" + getCurrentTimeString() + "] " + username + " joined the room\n";
                for (SOCKET otherSock : rooms[currentRoom]) {
                    if (otherSock != clientSock) {
                        send(otherSock, joinNotice.c_str(), (int)joinNotice.size(), 0);
                    }
                }
            }
            
            string notice = "[" + getCurrentTimeString() + "] You joined room: " + room + "\n";
            send(clientSock, notice.c_str(), (int)notice.size(), 0);
            continue;
        }
        else if (msg.rfind("/pm", 0) == 0) {
            string rest = msg.substr(4);
            string targetName = rest.substr(0, rest.find(" "));
            string text = rest.substr(rest.find(" ") + 1);

            SOCKET targetSock = INVALID_SOCKET;
            {
                lock_guard<mutex> lock(clientsMtx);
                for (auto &p : clients) {
                    if (p.second == targetName) {
                        targetSock = p.first;
                        break;
                    }
                }
            }

            if (targetSock != INVALID_SOCKET) {
                string pmToReceiver = "[" + getCurrentTimeString() + "][PM from " + username + "]: " + text + "\n";
                string pmToSender = "[" + getCurrentTimeString() + "][PM to " + targetName + "]: " + text + "\n";
                
                send(targetSock, pmToReceiver.c_str(), (int)pmToReceiver.size(), 0);
                send(clientSock, pmToSender.c_str(), (int)pmToSender.size(), 0);
            } else {
                string err = "[" + getCurrentTimeString() + "] User not found.\n";
                send(clientSock, err.c_str(), (int)err.size(), 0);
            }
            continue;
        }
        else if (msg == "/undo") {
            Message lastMsg;
            bool success = undoRedo.undo(lastMsg);
            
            if (success) {
                roomHistory.removeMessage(lastMsg.id);
                string notice = "[" + getCurrentTimeString() + "] Last message undone.\n";
                send(clientSock, notice.c_str(), (int)notice.size(), 0);
            } else {
                string notice = "[" + getCurrentTimeString() + "] No message to undo.\n";
                send(clientSock, notice.c_str(), (int)notice.size(), 0);
            }
            continue;
        }
        else if (msg == "/help") {
            string helpText = 
                "[" + getCurrentTimeString() + "] Available commands:\n"
                "/join <room>           - Join or create a chat room\n"
                "/pm <user> <message>   - Send private message to a user\n"
                "/reply <user> <msg>    - Reply publicly to a specific user in the room\n"
                "/undo                  - Undo your last message\n"
                "/redo                  - Redo your last undone message\n"
                "/history               - Show message history for current room\n"
                "/search <keyword>      - Search for messages containing keyword\n"
                "/quit                  - Exit the chat application\n"
                "/help                  - Show this help message\n";
            send(clientSock, helpText.c_str(), (int)helpText.size(), 0);
            continue;
        }
        else if (msg.rfind("/reply", 0) == 0) {
            string rest = msg.substr(7);
            string targetName = rest.substr(0, rest.find(" "));
            string text = rest.substr(rest.find(" ") + 1);
        
            SOCKET targetSock = INVALID_SOCKET;
            {
                lock_guard<mutex> lock(clientsMtx);
                for (auto &p : clients) {
                    if (p.second == targetName) {
                        targetSock = p.first;
                        break;
                    }
                }
            }
        
            if (targetSock != INVALID_SOCKET) {
                Message msgObj(messageCounter++, username, "-> " + targetName + ": " + text);
                roomHistory.addMessage(msgObj);
                undoRedo.addMessage(msgObj);
                messageQueue.push(msgObj);
            } else {
                string err = "[" + getCurrentTimeString() + "] User '" + targetName + "' not found.\n";
                send(clientSock, err.c_str(), (int)err.size(), 0);
            }
            continue;
        }
        else if (msg.rfind("/search", 0) == 0) {
            if (msg.length() <= 8) {
                string err = "[" + getCurrentTimeString() + "] Usage: /search <keyword>\n";
                send(clientSock, err.c_str(), (int)err.size(), 0);
                continue;
            }
            
            string keyword = msg.substr(8);
            auto searchResults = roomHistory.searchMessages(keyword);
            
            if (searchResults.empty()) {
                string result = "[" + getCurrentTimeString() + "] No messages found containing: '" + keyword + "'\n";
                send(clientSock, result.c_str(), (int)result.size(), 0);
            } else {
                string result = "[" + getCurrentTimeString() + "] Found " + to_string(searchResults.size()) + 
                               " message(s) containing '" + keyword + "':\n";
                for (const auto& msg : searchResults) {
                    result += msg.toString() + "\n";
                }
                send(clientSock, result.c_str(), (int)result.size(), 0);
            }
            continue;
        }
        else if (msg == "/redo") {
            Message redoMsg;
            bool success = undoRedo.redo(redoMsg);
            
            if (success) {
                roomHistory.addMessage(redoMsg);
                messageQueue.push(redoMsg);
                string notice = "[" + getCurrentTimeString() + "] Message redone.\n";
                send(clientSock, notice.c_str(), (int)notice.size(), 0);
            } else {
                string notice = "[" + getCurrentTimeString() + "] Nothing to redo.\n";
                send(clientSock, notice.c_str(), (int)notice.size(), 0);
            }
            continue;
        }
        else if (msg == "/history") {
            auto messages = roomHistory.getMessages();
            string historyText = "[" + getCurrentTimeString() + "] Message history:\n";
            
            for (const auto& msg : messages) {
                historyText += msg.toString() + "\n";
            }
            
            if (messages.empty()) {
                historyText = "[" + getCurrentTimeString() + "] No message history available.\n";
            }
            
            send(clientSock, historyText.c_str(), (int)historyText.size(), 0);
            continue;
        }

        // ================= Normal Message =================
        Message msgObj(messageCounter++, username, msg);
        roomHistory.addMessage(msgObj);
        undoRedo.addMessage(msgObj);
        messageQueue.push(msgObj);
    }
}

// ==========================
// Main
// ==========================

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "WSAStartup failed\n";
        return 1;
    }

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        cerr << "Socket creation failed: " << WSAGetLastError() << endl;
        WSACleanup();
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
        cerr << "Bind failed: " << WSAGetLastError() << endl;
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    if (listen(server_fd, SOMAXCONN) == SOCKET_ERROR) {
        cerr << "Listen failed: " << WSAGetLastError() << endl;
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    cout << "[" << getCurrentTimeString() << "] Chat server started on port " << PORT << endl;

    // Start broadcast worker thread
    thread broadcastThread(broadcastWorker);

    while (true) {
        SOCKET new_socket = accept(server_fd, nullptr, nullptr);
        if (new_socket == INVALID_SOCKET) {
            cerr << "[" << getCurrentTimeString() << "] Accept failed: " << WSAGetLastError() << endl;
            continue;
        }
        cout << "[" << getCurrentTimeString() << "] New connection accepted.\n";
        thread t(handleClient, new_socket);
        t.detach();
    }

    // Cleanup
    messageQueue.shutdownQueue();
    if (broadcastThread.joinable()) {
        broadcastThread.join();
    }
    
    closesocket(server_fd);
    WSACleanup();
    return 0;
}