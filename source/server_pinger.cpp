#include "server_pinger.h"

#include <curl/curl.h>

namespace {
size_t writeCallback(void* /*contents*/, size_t size, size_t nmemb, void* /*userp*/) {
    return size * nmemb; // discard body; only status is needed
}
}

bool ServerPinger::ping(const std::string& url,
                        long timeoutSeconds,
                        long* httpCodeOut,
                        std::string* errorOut) const {
    CURL* curl = curl_easy_init();
    if (!curl) {
        if (errorOut) *errorOut = "Failed to initialize CURL";
        if (httpCodeOut) *httpCodeOut = 0;
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);

    long code = 0;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    } else {
        if (errorOut) *errorOut = curl_easy_strerror(res);
    }

    curl_easy_cleanup(curl);

    if (httpCodeOut) *httpCodeOut = code;

    // success only on 2xx
    return (res == CURLE_OK) && (200 <= code && code < 300);
}
