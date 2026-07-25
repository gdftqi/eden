#ifndef __ADAM_UTILS_HTTP_CLIENT_HPP__
#define __ADAM_UTILS_HTTP_CLIENT_HPP__


#include <curl/curl.h>
#include <format>
#include <string>
#include "utils/log.hpp"


namespace adam::utils {


class Http {
    Http(const Http&) = delete;
    Http& operator=(const Http&) = delete;
    Http(Http&&) = delete;
    Http& operator=(Http&&) = delete;


public:
    typedef std::list<std::pair<std::string, std::string>> Header;


    static Http*
    instance() noexcept {
        static Http m;
        return &m;
    }


    ~Http() noexcept {
        ::curl_global_cleanup();
    }


    void
    set_timeout(uint32_t timeout) noexcept {
        if (timeout == 0) {
            timeout = 1;
        }

        timeout_ms_ = timeout * 1000;
    }


    template <typename T> int
    post(T* out, const char* url, const char* body, const Header& hdr = {}) noexcept {
        ::CURL* c = ::curl_easy_init();

        ::curl_easy_setopt(c, CURLOPT_URL, url);
        ::curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);

        ::curl_slist* hdrs = nullptr;
        for (auto& [k, v]: hdr) {
            hdrs = ::curl_slist_append(hdrs, std::format("{}: {}", k, v).c_str());
        }
        
        if (hdrs != nullptr) {
            ::curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
        }

        ::curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, Http::wr);

        std::string rsp;
        ::curl_easy_setopt(c, CURLOPT_WRITEDATA, &rsp);
        ::curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, timeout_ms_);
        ::CURLcode res = ::curl_easy_perform(c);

        if (hdrs != nullptr) {
            ::curl_slist_free_all(hdrs);
        }
        ::curl_easy_cleanup(c);

        if (res != CURLE_OK) {
            return -res;
        }

        if (out == nullptr) {
            return 0;
        }

        return T::deserialize(out, rsp);
    }


private:
    static size_t 
    wr(char* p, size_t s, size_t n, void* u) {
        ((std::string*)u)->append(p, s * n);
        return s * n;
    }


    explicit
    Http() noexcept {
        ASSERT(::curl_global_init(CURL_GLOBAL_DEFAULT) == ::CURLE_OK, "初始化 curl 失败");
    }


    uint32_t timeout_ms_ { 5000 };
}; // class Http;


} // namespace adam::utils


#endif // __ADAM_UTILS_HTTP_CLIENT_HPP__
