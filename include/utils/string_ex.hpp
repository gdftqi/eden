#ifndef __TYPHON_UTILS_STRING_EX_HPP__
#define __TYPHON_UTILS_STRING_EX_HPP__


#include <spdlog/fmt/bundled/ranges.h>
#include <spdlog/spdlog.h>


namespace typhon::utils {
    

inline std::string
bytes_to_hex(const uint8_t* data, size_t len) noexcept {
    return fmt::format("{:02x}", fmt::join(std::span(data, len), ""));
}



} // namespace typhon::utils;



#endif // __TYPHON_UTILS_STRING_EX_HPP__