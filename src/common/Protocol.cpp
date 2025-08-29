#include "common/Message.h"
#include <iostream>

namespace transport {

// Protocol utility functions
class Protocol {
public:
    static bool validateURN(const std::string& urn) {
        return urn.length() == 13 && urn.find_first_not_of("0123456789") == std::string::npos;
    }
    
    static bool validateURI(const std::string& uri) {
        return !uri.empty() && uri.length() <= 32;
    }
};

} // namespace transport
