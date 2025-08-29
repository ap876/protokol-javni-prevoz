#include "common/Logger.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <map>

namespace transport {

std::map<std::string, std::shared_ptr<Logger>> Logger::loggers_;
std::mutex Logger::loggers_mutex_;

Logger::Logger(const std::string& name) 
    : name_(name), log_level_(LogLevel::INFO) {}

Logger::~Logger() {
    if (file_stream_) {
        file_stream_->close();
    }
}

bool Logger::initialize(const std::string& log_file, LogLevel level) {
    log_level_ = level;
    
    if (!log_file.empty()) {
        setLogFile(log_file);
    }
    
    return true;
}

void Logger::setLogFile(const std::string& log_file) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    
    if (file_stream_) {
        file_stream_->close();
    }
    
    file_stream_ = std::make_unique<std::ofstream>(log_file, std::ios::app);
    if (!file_stream_->is_open()) {
        std::cerr << "Warning: Failed to open log file: " << log_file << std::endl;
        file_stream_.reset();
    }
}

void Logger::debug(const std::string& message) {
    log(LogLevel::DEBUG, message);
}

void Logger::info(const std::string& message) {
    log(LogLevel::INFO, message);
}

void Logger::warning(const std::string& message) {
    log(LogLevel::WARNING, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::ERROR, message);
}

void Logger::critical(const std::string& message) {
    log(LogLevel::CRITICAL, message);
}

void Logger::log(LogLevel level, const std::string& message) {
    if (level < log_level_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(log_mutex_);
    
    std::string formatted_message = formatMessage(level, message);
    
    // Always output to console for now
    std::cout << formatted_message << std::endl;
    
    // Output to file if available
    if (file_stream_ && file_stream_->is_open()) {
        *file_stream_ << formatted_message << std::endl;
        file_stream_->flush();
    }
}

std::string Logger::formatMessage(LogLevel level, const std::string& message) {
    std::ostringstream oss;
    
    oss << "[" << getCurrentTimestamp() << "] ";
    oss << "[" << levelToString(level) << "] ";
    oss << "[" << name_ << "] ";
    oss << message;
    
    return oss.str();
}

std::string Logger::getCurrentTimestamp() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:    return "DEBUG";
        case LogLevel::INFO:     return "INFO";
        case LogLevel::WARNING:  return "WARN";
        case LogLevel::ERROR:    return "ERROR";
        case LogLevel::CRITICAL: return "CRIT";
        default:                 return "UNKNOWN";
    }
}

std::shared_ptr<Logger> Logger::getLogger(const std::string& name) {
    std::lock_guard<std::mutex> lock(loggers_mutex_);
    
    auto it = loggers_.find(name);
    if (it != loggers_.end()) {
        return it->second;
    }
    
    auto logger = std::shared_ptr<Logger>(new Logger(name));
    loggers_[name] = logger;
    return logger;
}

} // namespace transport
