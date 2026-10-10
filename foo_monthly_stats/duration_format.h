#pragma once
#include <cmath>
#include <cstdio>
#include <string>

namespace fms
{
    inline std::string formatDuration(double seconds)
    {
        const long long rounded = seconds > 0.0 ? static_cast<long long>(std::llround(seconds)) : 0LL;
        const long long hours = rounded / 3600;
        const long long minutes = (rounded % 3600) / 60;
        const long long remainder = rounded % 60;
        char buffer[48] = {};
        std::snprintf(buffer, sizeof(buffer), "%02lld:%02lld:%02lld", hours, minutes, remainder);
        return buffer;
    }
} // namespace fms
