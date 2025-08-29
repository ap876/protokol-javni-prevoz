#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <memory>
#include <chrono>
#include <map>

namespace transport {

class Logger {
public:
    enum class LogLevel {
        DEBUG = 0,
        INFO = 1,
        WARNING = 2,
        ERROR = 3,
        CRITICAL = 4
    };

    Logger(const std::string& name = "Logger");
    ~Logger();

    bool initialize(const std::string& log_file = "", LogLevel level = LogLevel::INFO);
    void setLogLevel(LogLevel level) { log_level_ = level; }
    void setLogFile(const std::string& log_file);
    
    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    void critical(const std::string& message);
    
    void log(LogLevel level, const std::string& message);
    
    static std::shared_ptr<Logger> getLogger(const std::string& name);

private:
    std::string name_;
    LogLevel log_level_;
    std::unique_ptr<std::ofstream> file_stream_;
    std::mutex log_mutex_;
    
    std::string formatMessage(LogLevel level, const std::string& message);
    std::string getCurrentTimestamp();
    std::string levelToString(LogLevel level);
    
    static std::map<std::string, std::shared_ptr<Logger>> loggers_;
    static std::mutex loggers_mutex_;
};

} // namespace transport
