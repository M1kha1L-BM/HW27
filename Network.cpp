#include "Network.h"
#include <cstring>
#include <chrono>
#include <thread>

Network::Network(Logger& loggerRef)
    : running(false), logger(loggerRef) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

Network::~Network() {
    stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

void Network::cleanupSocket(SOCKET sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

bool Network::startServer(unsigned short port) {
    running = true;
    serverThread = std::thread(&Network::serverLoop, this, port);
    return true;
}

void Network::serverLoop(unsigned short port) {
    SOCKET serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock == INVALID_SOCKET) {
        std::cerr << "[Ошибка] Не удалось создать сокет сервера\n";
        logger.writeLog("[Ошибка] Не удалось создать сокет сервера");
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "[Ошибка] bind()\n";
        logger.writeLog("[Ошибка] bind()");
        cleanupSocket(serverSock);
        return;
    }

    listen(serverSock, 5);
    std::cout << "[Сервер] Ожидание подключений на порту " << port << "...\n";
    logger.writeLog("[Сервер] Ожидание подключений на порту " + std::to_string(port));

    while (running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(serverSock, &readfds);

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 200000; // 200 мс таймаут

        int activity = select(serverSock + 1, &readfds, nullptr, nullptr, &tv);
        if (activity < 0) {
            std::cerr << "[Ошибка] select()\n";
            logger.writeLog("[Ошибка] select()");
            break;
        }

        if (activity > 0 && FD_ISSET(serverSock, &readfds)) {
            sockaddr_in clientAddr{};
#ifdef _WIN32
            int clientLen = sizeof(clientAddr);
#else
            socklen_t clientLen = sizeof(clientAddr);
#endif
            SOCKET clientSock = accept(serverSock, (sockaddr*)&clientAddr, &clientLen);
            if (clientSock != INVALID_SOCKET) {
                logger.writeLog("[Сервер] Новый клиент подключился");
                std::thread(&Network::handleClient, this, clientSock).detach();
            }
        }
        // если activity == 0 — просто таймаут, проверяем running и продолжаем
    }

    cleanupSocket(serverSock);
    logger.writeLog("[Сервер] Завершение работы");
}

void Network::handleClient(SOCKET clientSocket) {
    char buffer[1024];
    while (running) {
        int bytes = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) {
            std::cerr << "[Сервер] Клиент отключился или ошибка recv()\n";
            logger.writeLog("[Сервер] Клиент отключился или ошибка recv()");
            break;
        }

        buffer[bytes] = '\0';
        std::string msg(buffer);

        if (msg.empty()) {
            std::cerr << "[Сервер] Получено пустое сообщение — игнорируем\n";
            logger.writeLog("[Сервер] Получено пустое сообщение — игнорируем");
            continue;
        }
        if (msg.size() > 500) {
            std::cerr << "[Сервер] Сообщение слишком длинное — игнорируем\n";
            logger.writeLog("[Сервер] Сообщение слишком длинное — игнорируем");
            continue;
        }

        std::cout << "\n[Получено] " << msg << "\n> ";
        logger.writeLog("[Получено от сети] " + msg);
    }
    cleanupSocket(clientSocket);
}

bool Network::connectToPeer(const std::string& ip, unsigned short port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
#ifdef _WIN32
    InetPtonA(AF_INET, ip.c_str(), &addr.sin_addr);
#else
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
#endif

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "[Ошибка] Не удалось подключиться к " << ip << ":" << port << "\n";
        logger.writeLog("[Ошибка] Не удалось подключиться к " + ip + ":" + std::to_string(port));
        cleanupSocket(sock);
        return false;
    }

    std::cout << "[Подключено] " << ip << ":" << port << "\n";
    logger.writeLog("[Подключено] " + ip + ":" + std::to_string(port));
    std::lock_guard<std::mutex> lock(peersMutex);
    peerSockets.push_back(sock);
    return true;
}

void Network::sendMessageToPeers(const std::string& msg) {
    std::lock_guard<std::mutex> lock(peersMutex);
    for (auto sock : peerSockets) {
        int sent = send(sock, msg.c_str(), (int)msg.size(), 0);
        if (sent == SOCKET_ERROR) {
            std::cerr << "[Ошибка] Не удалось отправить сообщение одному из клиентов\n";
            logger.writeLog("[Ошибка] Не удалось отправить сообщение одному из клиентов");
        }
    }
    logger.writeLog("[Отправлено в сеть] " + msg);
}

void Network::stop() {
    running = false;
    std::lock_guard<std::mutex> lock(peersMutex);
    for (auto sock : peerSockets)
        cleanupSocket(sock);
    peerSockets.clear();

    if (serverThread.joinable())
        serverThread.join();
}