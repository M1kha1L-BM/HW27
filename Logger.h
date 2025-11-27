#pragma once
#include <fstream>
#include <string>
#include <shared_mutex>

class Logger {
private:
    std::fstream file;
    mutable std::shared_mutex mutex;

public:
    Logger(const std::string& filename = "log.txt") {
        file.open(filename, std::ios::in | std::ios::out | std::ios::app);
        if (!file.is_open()) {
            file.clear();
            file.open(filename, std::ios::out);
            file.close();
            file.open(filename, std::ios::in | std::ios::out | std::ios::app);
        }
    }

    ~Logger() {
        if (file.is_open()) {
            file.close();
        }
    }

    void writeLog(const std::string& line) {
        std::unique_lock lock(mutex);
        file.clear();
        file.seekp(0, std::ios::end);
        file << line << std::endl;
        file.flush();
    }

    bool readLog(std::string& line) {
        std::shared_lock lock(mutex);
        if (!std::getline(file, line)) {
            return false;
        }
        return true;
    }

    void resetRead() {
        std::unique_lock lock(mutex);
        file.clear();
        file.seekg(0, std::ios::beg);
    }
};
