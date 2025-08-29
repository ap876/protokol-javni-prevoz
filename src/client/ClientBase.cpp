#include "client/ClientBase.h"
#include "common/Logger.h"
#include <iostream>
#include <thread>
#include <chrono>

namespace transport {

ClientBase::ClientBase(const std::string& client_name) 
    : client_name_(client_name), logger_(std::make_shared<Logger>()) {
}

void ClientBase::logInfo(const std::string& message) {
    if (logger_) {
        logger_->log(Logger::LogLevel::INFO, "[" + client_name_ + "] " + message);
    }
}

void ClientBase::logError(const std::string& message) {
    if (logger_) {
        logger_->log(Logger::LogLevel::ERROR, "[" + client_name_ + "] " + message);
    }
}

} // namespace transport
