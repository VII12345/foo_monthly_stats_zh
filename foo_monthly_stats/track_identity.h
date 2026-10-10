#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace fms
{
    // Keep this normalization byte-compatible with trackIdentitySql() in db_manager.cpp.
    inline std::string normalizeIdentityField(const std::string &value)
    {
        const auto first = value.find_first_not_of(' ');
        if (first == std::string::npos)
            return {};
        const auto last = value.find_last_not_of(' ');
        std::string result = value.substr(first, last - first + 1);
        for (char &ch : result)
        {
            const auto byte = static_cast<unsigned char>(ch);
            if (byte >= 'A' && byte <= 'Z')
                ch = static_cast<char>(byte + ('a' - 'A'));
        }
        return result;
    }

    inline uint64_t trackPathCrc64(const std::string &path)
    {
        constexpr uint64_t poly = 0xad93d23594c935a9ULL;
        uint64_t crc = 0;
        for (unsigned char byte : path)
        {
            crc ^= byte;
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc & 1) ? ((crc >> 1) ^ poly) : (crc >> 1);
        }
        return crc;
    }

    inline std::string makeTrackIdentity(const std::string &title, const std::string &artist, const std::string &path)
    {
        const auto normalizedTitle = normalizeIdentityField(title);
        const auto normalizedArtist = normalizeIdentityField(artist);
        if (!normalizedTitle.empty() && !normalizedArtist.empty())
            return "meta:" + normalizedTitle + '\x1f' + normalizedArtist;

        char crc[17] = {};
        std::snprintf(crc, sizeof(crc), "%016llx", static_cast<unsigned long long>(trackPathCrc64(path)));
        return std::string("path:") + crc;
    }
} // namespace fms
