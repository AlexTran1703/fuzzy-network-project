#pragma once

#include <iostream>
#include <mutex>
#include <fstream>
#include <string>
#include <sstream>
#include <filesystem>
#include <ctime>
#include "nlohmann/json.hpp"

namespace SystemModel {
    class PhysicalNode;
    class PhysicalGroup;
    class LogicalNode;
    class LogicalGroup;
}

using json = nlohmann::json;

// Logger class for plain text logs
class FileLogger
{
public:
    static FileLogger& instance();
    void log(const std::string& level, const std::string& msg);
    void set_filename(const std::string& filename);
private:
    FileLogger();
    ~FileLogger();
    FileLogger(const FileLogger&) = delete;
    FileLogger& operator=(const FileLogger&) = delete;
    std::mutex mtx_;
    std::string filename_;
    bool initialized_;
    void init();
};

// Macros for logging
#define LOG_INFO(msg) \
    do { \
        std::ostringstream oss; \
        oss << msg; \
        FileLogger::instance().log("INFO", oss.str()); \
    } while(0)
#define LOG_WARN(msg) \
    do { \
        std::ostringstream oss; \
        oss << msg; \
        FileLogger::instance().log("WARN", oss.str()); \
    } while(0)
#define LOG_ERROR(msg) \
    do { \
        std::ostringstream oss; \
        oss << msg; \
        FileLogger::instance().log("ERROR", oss.str()); \
    } while(0)
    
namespace Utils
{
    json to_json(const SystemModel::PhysicalNode &node);
    json to_json(const SystemModel::PhysicalGroup &group);
    json to_json(const SystemModel::LogicalNode &ln);
    json to_json(const SystemModel::LogicalGroup &group);

    class JsonStateLogger
    {
    public:
        static JsonStateLogger &instance();
        void log(const nlohmann::json &j);
        std::string filename() const;
    private:
        JsonStateLogger();
        ~JsonStateLogger();
        JsonStateLogger(const JsonStateLogger &) = delete;
        JsonStateLogger &operator=(const JsonStateLogger &) = delete;
        std::mutex mtx_;
        std::string filename_;
        bool first_;
    };

    void log_state_json(const nlohmann::json &j);
}