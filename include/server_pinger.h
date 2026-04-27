#pragma once

#include <string>

class ServerPinger {
public:
    // Returns true if the request completes with a 2xx HTTP code.
    // Optionally returns the HTTP status and an error message.
    bool ping(const std::string& url,
              long timeoutSeconds = 5,
              long* httpCodeOut = nullptr,
              std::string* errorOut = nullptr) const;
};
