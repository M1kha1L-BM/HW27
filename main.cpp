#include "Chat.h"
#include <iostream>

void printSystemInfo();

int main() {

    setlocale(LC_ALL, "Russian");

    printSystemInfo();

    Chat chat;
    chat.run();

    return 0;
}