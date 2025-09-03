# 💬 Chat Application in C++ (Winsock)

[![C++](https://img.shields.io/badge/Language-C++-blue.svg)](https://isocpp.org/) 
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![GitHub stars](https://img.shields.io/github/stars/YafetGetu/Chat-Application-CPP?style=social)](https://github.com/YafetGetu/Chat-Application-CPP/stargazers)
[![GitHub forks](https://img.shields.io/github/forks/YafetGetu/Chat-Application-CPP?style=social)](https://github.com/YafetGetu/Chat-Application-CPP/network/members)

A real-time multi-room chat application built in **C++** using **Winsock**.  
Supports multiple clients, private messaging, undo/redo, search, and chat history.

---

## ✨ Features
- 🔗 **Multi-room chat**: Join or create rooms dynamically  
- 👤 **Private messaging**: Send direct messages to specific users  
- ↩️ **Undo & Redo**: Undo your last message and redo if needed  
- 📜 **Message history**: View conversation history in a room  
- 🔍 **Search**: Find messages by keyword  
- 🚪 **/quit command**: Clean exit from the server  
- 🧵 **Multithreaded**: Uses threads for concurrent communication  

---

## 📂 Project Structure
- project/
- │── main_client.cpp # Client-side source code
- │── main_server.cpp # Server-side source code
- │── README.md # Project documentation
- │── .gitignore # Ignored files (build, binaries, zips)


---

## ⚙️ Requirements
- Windows OS  
- C++17 or later  
- Winsock2 library  

---

## 🚀 How to Run

### 1. Compile the server
```bash
g++ main_server.cpp -o server -lws2_32

```

---

##  Chat Commands

/join <room>           - Join or create a chat room
/pm <user> <message>   - Send private message to a user
/reply <user> <msg>    - Reply to a user in the current room
/undo                  - Undo your last message
/redo                  - Redo your last undone message
/history               - Show message history for current room
/search <keyword>      - Search for messages containing keyword
/quit                  - Exit the chat application
/help                  - Show help menu

