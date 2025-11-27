#pragma once
#include <memory>
#include <vector>
#include <string>
#include "User.h"
#include "Message.h"
#include "Network.h"
#include "Logger.h"
#include <libpq-fe.h>

class Chat {
private:
    std::vector<std::shared_ptr<User>> users;
    std::vector<Message> allMessages;
    std::shared_ptr<User> loggedInUser = nullptr;
    Logger logger; // כמדדונ
    Network network;
    PGconn* conn;

   
    std::shared_ptr<User> findUserByLogin(const std::string& login);

    void loadUsers();
    void loadMessages();

public:
    Chat();
    ~Chat();

    void registerUser();
    void login();
    void logout();
    void sendMessage();
    void checkMessages();
    void startNetworking();
    void run();
    void showLog(); // ןנמסלמענ כמדא
};