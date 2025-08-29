#pragma once

#include "common/Message.h"
#include "common/Logger.h"
#include <string>
#include <memory>

namespace transport {

class ClientBase {
public:
    ClientBase(const std::string& client_name);
    virtual ~ClientBase() = default;
    
    virtual bool connect(const std::string& server, int port) = 0;
    virtual void disconnect() = 0;

protected:
    virtual void handleMessage(std::unique_ptr<Message> message) = 0;
    
    void logInfo(const std::string& message);
    void logError(const std::string& message);

private:
    std::string client_name_;
    std::shared_ptr<Logger> logger_;
};

} // namespace transport
