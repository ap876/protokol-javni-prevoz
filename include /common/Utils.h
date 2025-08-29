#pragma once

#include <string>

namespace transport {

class Utils {
public:
    static std::string generateUUID();
    static std::string getCurrentTimestamp();
};

} // namespace transport
