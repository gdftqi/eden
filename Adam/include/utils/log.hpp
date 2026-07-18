#ifndef __ADAM_UTILS_LOG_HPP__
#define __ADAM_UTILS_LOG_HPP__


// 与 adam Makefile 的 -DSPDLOG_COMPILED_LIB 配套:
// 下游 include 本 header 时自动获得 compiled 模式,避免 header-only inline
// 与 libadam.a 内的 compiled spdlog 符号产生 weak/strong 混用。
#ifndef SPDLOG_COMPILED_LIB
#define SPDLOG_COMPILED_LIB 1
#endif

#include <spdlog/spdlog.h>


namespace adam::utils {


void
init_log(const std::string& log_path);


inline void
disable_log() noexcept {
    spdlog::set_level(spdlog::level::off);
}


inline void
set_log_level(spdlog::level::level_enum level) noexcept {
    spdlog::set_level(level);
}


} // namespace adam::utils


// 日志宏定义
#define xDEBUG(...)   SPDLOG_DEBUG(__VA_ARGS__)
#define xINFO(...)    SPDLOG_INFO(__VA_ARGS__)
#define xWARN(...)    SPDLOG_WARN(__VA_ARGS__)
#define xERROR(...)   SPDLOG_ERROR(__VA_ARGS__)
#define xFATAL(...)   do { SPDLOG_CRITICAL(__VA_ARGS__); std::abort(); } while(0)

#define ASSERT(expr, fmt, ...) \
    do { \
        if (!(expr)) { \
            xFATAL("Assertion failed: {} | " fmt, #expr, ##__VA_ARGS__); \
        } \
    } while (0)


#endif // __ADAM_UTILS_LOG_HPP__