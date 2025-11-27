#include "Chat.h"
#include "Message.h"
#include "User.h"
#include "sha256.h"
#include <iostream>
#include <libpq-fe.h>
#include <random>
#include <sstream>
#include <iomanip>

using namespace std;

// Генерация случайной соли
std::string generateSalt(size_t length = 16) {
    static const char chars[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, sizeof(chars) - 2);

    std::string salt;
    for (size_t i = 0; i < length; ++i) {
        salt += chars[dist(gen)];
    }
    return salt;
}

Chat::Chat()
    : logger("log.txt"), network(logger) {
    conn = PQconnectdb("host=localhost dbname=postgres user=postgres password=12345678");
    if (PQstatus(conn) != CONNECTION_OK) {
        cerr << "Ошибка подключения к БД: " << PQerrorMessage(conn) << endl;
        exit(1);
    }
    loadUsers();
    loadMessages();
}

Chat::~Chat() {
    PQfinish(conn);
    network.stop();
}

shared_ptr<User> Chat::findUserByLogin(const string& login) {
    for (auto& user : users)
        if (user->getLogin() == login)
            return user;
    return nullptr;
}

void Chat::loadUsers() {
    const char* query = "SELECT login, password, salt, name FROM users";
    PGresult* res = PQexecParams(conn, query, 0, nullptr, nullptr, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        cerr << "Ошибка SELECT users: " << PQerrorMessage(conn) << endl;
        PQclear(res);
        return;
    }

    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++) {
        string login = PQgetvalue(res, i, 0);
        string password = PQgetvalue(res, i, 1);
        string salt = PQgetvalue(res, i, 2);
        string name = PQgetvalue(res, i, 3);
        users.push_back(make_shared<User>(login, password, salt, name));
    }
    PQclear(res);
}

void Chat::loadMessages() {
    const char* query =
        "SELECT m.sender, u.name, m.recipient, m.is_private, m.text "
        "FROM messages m "
        "JOIN users u ON m.sender = u.login "
        "ORDER BY m.id";

    PGresult* res = PQexecParams(conn, query, 0, nullptr, nullptr, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        cerr << "Ошибка SELECT messages: " << PQerrorMessage(conn) << endl;
        PQclear(res);
        return;
    }

    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++) {
        string senderLogin = PQgetvalue(res, i, 0);
        string senderName = PQgetvalue(res, i, 1);
        string recipient = PQgetvalue(res, i, 2);
        bool isPrivate = (string(PQgetvalue(res, i, 3)) == "t");
        string text = PQgetvalue(res, i, 4);

        Message msg(senderLogin, recipient, text, isPrivate);
        msg.setSenderName(senderName);

        allMessages.push_back(msg);

        if (recipient != "all") {
            auto user = findUserByLogin(recipient);
            if (user) user->addMessage(msg);
        }
        else {
            for (auto& user : users)
                if (user->getLogin() != senderLogin)
                    user->addMessage(msg);
        }
    }
    PQclear(res);
}

void Chat::registerUser() {
    cout << "Регистрация нового пользователя" << endl;
    string login, password, name;

    cout << "Введите логин: ";
    cin >> login;
    if (findUserByLogin(login)) {
        cout << "Такой логин уже существует" << endl;
        return;
    }

    cout << "Введите пароль: ";
    cin >> password;
    string salt = generateSalt();
    string hashedPassword = sha256(password + salt);

    cout << "Введите имя: ";
    cin.ignore();
    getline(cin, name);

    const char* query =
        "INSERT INTO users (login, password, salt, name) VALUES ($1, $2, $3, $4)";
    const char* paramValues[4] = { login.c_str(), hashedPassword.c_str(), salt.c_str(), name.c_str() };

    PGresult* res = PQexecParams(conn, query, 4, nullptr, paramValues, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        cerr << "Ошибка INSERT user: " << PQerrorMessage(conn) << endl;
    }
    PQclear(res);

    users.push_back(make_shared<User>(login, hashedPassword, salt, name));
    cout << "Пользователь зарегистрирован" << endl;
}

void Chat::login() {
    cout << "Вход в систему" << endl;
    string login, password;
    cout << "Логин: ";
    cin >> login;
    cout << "Пароль: ";
    cin >> password;

    auto user = findUserByLogin(login);
    if (user) {
        string hashedPassword = sha256(password + user->getSalt());
        if (user->getPassword() == hashedPassword) {
            loggedInUser = user;
            cout << "Добро пожаловать, " << user->getName() << "!" << endl;
        }
        else {
            cout << "Неверный логин или пароль" << endl;
        }
    }
    else {
        cout << "Неверный логин или пароль" << endl;
    }
}

void Chat::logout() {
    if (loggedInUser) {
        cout << "Выход: " << loggedInUser->getName() << endl;
        loggedInUser = nullptr;
    }
    else {
        cout << "Вы не вошли в систему" << endl;
    }
}

void Chat::sendMessage() {
    if (!loggedInUser) {
        cout << "Сначала войдите в систему" << endl;
        return;
    }

    string recipient, text;
    cout << "Кому (login или all): ";
    cin >> recipient;
    cout << "Текст: ";
    cin.ignore();
    getline(cin, text);

    bool isPrivate = (recipient != "all");
    Message msg(loggedInUser->getLogin(), recipient, text, isPrivate);
    msg.setSenderName(loggedInUser->getName());

    const char* query =
        "INSERT INTO messages (sender, recipient, is_private, text) VALUES ($1, $2, $3, $4)";
    const char* paramValues[4] = {
        loggedInUser->getLogin().c_str(),
        recipient.c_str(),
        isPrivate ? "true" : "false",
        text.c_str()
    };

    PGresult* res = PQexecParams(conn, query, 4, nullptr, paramValues, nullptr, nullptr, 0);
    PQclear(res);

    allMessages.push_back(msg);
    if (recipient != "all") {
        auto user = findUserByLogin(recipient);
        if (user) user->addMessage(msg);
    }
    else {
        for (auto& user : users)
            if (user->getLogin() != loggedInUser->getLogin())
                user->addMessage(msg);
    }

    network.sendMessageToPeers(text);
    logger.writeLog("[Отправлено] " + text);
}

void Chat::checkMessages() {
    if (!loggedInUser) {
        cout << "Сначала войдите в систему" << endl;
        return;
    }
    loggedInUser->showInbox();
}

void Chat::startNetworking() {
    unsigned short port;
    cout << "Введите порт сервера: ";
    cin >> port;
    network.startServer(port);

    string ip;
    cout << "Введите IP для подключения (или пусто): ";
    cin.ignore();
    getline(cin, ip);
    if (!ip.empty()) {
        unsigned short peerPort;
        cout << "Введите порт: ";
        cin >> peerPort;
        network.connectToPeer(ip, peerPort);
    }
}

void Chat::showLog() {
    logger.resetRead();
    std::string line;
    while (logger.readLog(line)) {
        std::cout << line << std::endl;
    }
}

void Chat::run() {
    startNetworking();

    while (true) {
        cout << "\n--- Меню ---\n";
        cout << "1. Регистрация\n";
        cout << "2. Вход\n";
        cout << "3. Отправить сообщение\n";
        cout << "4. Проверить сообщения\n";
        cout << "5. Выйти из аккаунта\n";
        cout << "6. Выход из программы\n";
        cout << "Выберите действие: ";

        string choice;
        cin >> choice;

        if (choice == "1") registerUser();
        else if (choice == "2") login();
        else if (choice == "3") sendMessage();
        else if (choice == "4") checkMessages();
        else if (choice == "5") logout();
        else if (choice == "6") {
            cout << "Сохранение данных и выход..." << endl;
            break;
        }
        else cout << "Неверный ввод" << endl;
    }
}