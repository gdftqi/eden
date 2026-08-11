#ifndef __ADAM_UTILS_ETCD_HPP__
#define __ADAM_UTILS_ETCD_HPP__


#include <string>
#include <curl/curl.h>
#include <yaml-cpp/yaml.h>

// ikcp.h 定义了 INLINE 宏
#pragma push_macro("INLINE")
#undef INLINE
#include "simdjson.h"
#pragma pop_macro("INLINE")

#include "utils/log.hpp"
#include "core/error.hpp"


namespace adam::utils {


struct EtcdConfig {
    std::string url;
    std::string user;
    std::string pass;
    int         ttl;

    
    int
    from_yaml(const YAML::Node& root) noexcept {
        for (const char* k : { "url", "user", "pass" }) {
            if (!root[k]) {
                xERROR("etcd 配置缺字段: {}", k);
                return xERR_CONF_MISSING;
            }
        }

        url  = root["url"].as<std::string>();
        user = root["user"].as<std::string>();
        pass = root["pass"].as<std::string>();
        ttl  = root["ttl"].as<int>();

        if (url.empty()) {
            xERROR("etcd.url 不能为空");
            return xERR_CONF_VALUE;
        }

        if (user.empty()) {
            xERROR("etcd.user 不能为空");
            return xERR_CONF_VALUE;
        }

        if (pass.empty()) {
            xERROR("etcd.pass 不能为空");
            return xERR_CONF_VALUE;
        }

        if (ttl <= 0) {
            xERROR("etcd.ttl 必须为正: {}", ttl);
            return xERR_CONF_VALUE;
        }

        return xOK;
    }
}; // struct EtcdConfig;


struct EtcdRsp {
    int code { 0 };
    std::string message;
    std::string token;
    std::string id;
    std::string ttl;
    std::list<std::pair<std::string, std::string>> kvs;

    struct {
        std::string cluster_id;
        std::string member_id;
        std::string revision;
        std::string raft_term;
    } header;


    static int
    deserialize(EtcdRsp* out, const std::string& json) noexcept;


    void
    reset() noexcept {
        *this = EtcdRsp{};
    }
}; // struct EtcdRsp;


struct EtcdTls {
    uint64_t lease       { 0 };
    uint64_t last_update { 0 };
    uint64_t last_auth   { 0 };
    char     token[512]  { 0 };


    EtcdTls() = default;
    ~EtcdTls() = default;
    EtcdTls(const EtcdTls&) = delete;
    EtcdTls(EtcdTls&&) = delete;
    EtcdTls& operator=(const EtcdTls&) = delete;
    EtcdTls& operator=(EtcdTls&&) = delete;
}; // struct EtcdTls;


int
etcd_auth(EtcdRsp* rsp, const char* url, const char* user, const char* pwd) noexcept;


int
etcd_grant(EtcdRsp* rsp, const char* url, int ttl) noexcept;


int
etcd_put(EtcdRsp* rsp, const char* url, const char* token, const char* key, const char* val, const char* lease) noexcept;


int
etcd_keepalive(EtcdRsp* rsp, const char* url, const char* token, const char* lease_id) noexcept;


int
etcd_delete(EtcdRsp* rsp, const char* url, const char* token, const char* key) noexcept;


int
etcd_get_prefix(EtcdRsp* rsp, const char* url, const char* token, const char* prefix) noexcept;


} // namespace adam::utils


#endif // __ADAM_UTILS_ETCD_HPP__
