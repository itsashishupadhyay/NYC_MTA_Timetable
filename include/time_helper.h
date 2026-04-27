#pragma once

#include <string>
#include <chrono>
#include <optional>

class TimeHelper {
public:
    static std::string formatTime(const std::optional<std::chrono::system_clock::time_point>& tp);
    static std::string formatHHMM(const std::chrono::system_clock::time_point& tp);
    static std::string nowDateTime();
    static std::string nowDateTimeEastern();
    static int minutesDiff(const std::chrono::system_clock::time_point& a,
                           const std::chrono::system_clock::time_point& b);
    static std::chrono::system_clock::time_point unixToTimePoint(std::int64_t t);
};
