#include "time_helper.h"
#include <string>
#include <chrono>
#include <optional>
#include <ctime>
#include <cstdlib>

// Convert a time_t to Eastern Time (America/New_York handles EST/EDT automatically)
static std::tm toEastern(std::time_t t) {
    const char* oldTZ = std::getenv("TZ");
    std::string oldTZCopy = oldTZ ? std::string(oldTZ) : std::string();

    setenv("TZ", "America/New_York", 1);
    tzset();

    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    if (!oldTZCopy.empty()) {
        setenv("TZ", oldTZCopy.c_str(), 1);
    } else {
        unsetenv("TZ");
    }
    tzset();

    return tm;
}

std::string TimeHelper::formatTime(const std::optional<std::chrono::system_clock::time_point>& tp) {
    if (!tp.has_value()) return "—";

    std::time_t t = std::chrono::system_clock::to_time_t(tp.value());
    std::tm tm = toEastern(t);

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
    return buffer;
}

std::string TimeHelper::formatHHMM(const std::chrono::system_clock::time_point& tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = toEastern(t);

    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M", &tm);
    return std::string(buf);
}

std::string TimeHelper::nowDateTime() {
    std::time_t t = std::time(nullptr);
    std::tm tm = toEastern(t);

    char buf[64];
    std::strftime(buf, sizeof(buf), "%b %d, %Y  %H:%M", &tm);
    return std::string(buf);
}

std::string TimeHelper::nowDateTimeEastern() {
    return nowDateTime();
}

int TimeHelper::minutesDiff(const std::chrono::system_clock::time_point& a,
                            const std::chrono::system_clock::time_point& b) {
    return (int)std::chrono::duration_cast<std::chrono::minutes>(b - a).count();
}

std::chrono::system_clock::time_point TimeHelper::unixToTimePoint(std::int64_t t) {
    using namespace std::chrono;
    return system_clock::time_point{seconds{t}};
}
