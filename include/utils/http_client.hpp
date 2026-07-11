#ifndef __TYPHON_UTILS_HTTP_CLIENT_HPP__
#define __TYPHON_UTILS_HTTP_CLIENT_HPP__


#include <curl/curl.h>
#include <format>
#include <string>
#include "utils/log.hpp"


namespace typhon::utils {


/**
 * 初始化 HTTP 客户端库
 */
inline int
init_http() noexcept {
    static bool initialized = false;

    ::CURLcode res = ::CURLE_OK;
    if (!initialized) {
        res = curl_global_init(CURL_GLOBAL_DEFAULT);
        initialized = true;
    }

    return res;
}


inline size_t 
http_wr(char* p, size_t s, size_t n, void* u) {
    ((std::string*)u)->append(p, s * n);
    return s * n;
}


template <typename T> int
http_post(T* out, const char* url, const char* body, const char** keys, const char** values, size_t hd_num) noexcept {
    ASSERT(out != nullptr, "out 不能为null");
    
    init_http();
    ::CURL* c = ::curl_easy_init();
    ASSERT(c != nullptr, "Failed to initialize CURL");

    ::curl_easy_setopt(c, CURLOPT_URL, url);
    ::curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);

    ::curl_slist* hdrs = nullptr;
    for (size_t i = 0; i < hd_num; ++i) {
        hdrs = ::curl_slist_append(hdrs, std::format("{}: {}", keys[i], values[i]).c_str());
    }

    if (hdrs) {
        ::curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    }

    ::curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, http_wr);

    std::string rsp;
    ::curl_easy_setopt(c, CURLOPT_WRITEDATA, &rsp);
    ::curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 5000L);
    ::CURLcode rc = ::curl_easy_perform(c);
    ::curl_slist_free_all(hdrs);
    ::curl_easy_cleanup(c);
    
    if (rc != CURLE_OK) {
        xERROR("HTTP POST failed: {}", ::curl_easy_strerror(rc));
        return -1;
    }

    if (out == nullptr) {
        return 0;
    }

    return T::deserialize(out, rsp);
}


} // namespace typhon::utils


#endif // __TYPHON_UTILS_HTTP_CLIENT_HPP__