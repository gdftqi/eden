#ifndef __TYPHON_UTILS_STRING_EX_HPP__
#define __TYPHON_UTILS_STRING_EX_HPP__


#include <spdlog/fmt/bundled/ranges.h>
#include <spdlog/spdlog.h>
#include <sodium.h>


namespace typhon::utils {
    

inline std::string
bytes_to_hex(const uint8_t* data, size_t len) noexcept {
    return fmt::format("{:02x}", fmt::join(std::span(data, len), ""));
}


inline int
base64_encode(std::string& s, const uint8_t* data, size_t len) noexcept {
    size_t size = sodium_base64_ENCODED_LEN(len, sodium_base64_VARIANT_ORIGINAL);
    s.resize(size);
    ::sodium_bin2base64(s.data(), size, data, len, sodium_base64_VARIANT_ORIGINAL);
    s.resize(size - 1);
    return 0;
}


inline size_t
base64_decode_len(size_t b64_len) noexcept {
    return b64_len / 4 * 3;
}


inline int
base64_decode(const std::string& s, uint8_t* data, size_t* len) noexcept {
    return ::sodium_base642bin(data, *len, s.data(), s.length(), nullptr, len, nullptr, sodium_base64_VARIANT_ORIGINAL);
}


inline std::string
base64_decode(const std::string& s) noexcept {
    std::string out(base64_decode_len(s.length()), '\0');
    size_t len = out.size();
    if (::sodium_base642bin((uint8_t*)out.data(), len, s.data(), s.length(), nullptr, &len, nullptr, sodium_base64_VARIANT_ORIGINAL) != 0) {
        return "";
    }

    out.resize(len);
    return out;
}


} // namespace typhon::utils;


#endif // __TYPHON_UTILS_STRING_EX_HPP__