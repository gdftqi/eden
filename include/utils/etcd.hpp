#ifndef __TYPHON_UTILS_ETCD_HPP__
#define __TYPHON_UTILS_ETCD_HPP__


#include <curl/curl.h>
#include <format>
#include <string>
#include <string_view>

// ikcp.h 定义了 INLINE 宏, 会撞烂 simdjson 里的枚举成员 INLINE(layout_mode);
// 此处临时摘掉宏再包含, 完了恢复, 与包含顺序无关
#pragma push_macro("INLINE")
#undef INLINE
#include "simdjson.h"
#pragma pop_macro("INLINE")

#include "utils/log.hpp"


namespace typhon::utils {


struct EtcdRsp {
    int code { 0 };
    std::string message;
    std::string token;
    std::string id;
    std::string ttl;

    struct {
        std::string cluster_id;
        std::string member_id;
        std::string revision;
        std::string raft_term;
    } header;


    static int
    deserialize(EtcdRsp* out, const std::string& json) noexcept {
        simdjson::ondemand::parser parser;
        auto j = simdjson::padded_string(json);
        auto root = parser.iterate(j);

        if (root["code"].has_value()) {
            out->code = root["code"].get_int32().value_unsafe();
        }

        if (root["message"].has_value()) {
            out->message = std::string(root["message"].get_string().value_unsafe());
        }

        if (root["token"].has_value()) {
            out->token = std::string(root["token"].get_string().value_unsafe());
        }

        if (root["ID"].has_value()) {
            out->id = std::string(root["ID"].get_string().value_unsafe());
        }

        if (root["TTL"].has_value()) {
            out->ttl = std::string(root["TTL"].get_string().value_unsafe());
        }

        if (root["header"].has_value()) {
            auto h = root["header"];
            if (h["cluster_id"].has_value()) {
                out->header.cluster_id = std::string(h["cluster_id"].get_string().value_unsafe());
            }

            if (h["member_id"].has_value()) {
                out->header.member_id = std::string(h["member_id"].get_string().value_unsafe());
            }

            if (h["revision"].has_value()) {
                out->header.revision = std::string(h["revision"].get_string().value_unsafe());
            }

            if (h["raft_term"].has_value()) {
                out->header.raft_term = std::string(h["raft_term"].get_string().value_unsafe());
            }
        }

        return 0;
    }
}; // struct EtcdRsp;


int
etcd_auth(EtcdRsp* rsp, const char* base_url, const char* user, const char* pwd) noexcept;


int
etcd_grant_put(EtcdRsp* rsp, const char* base_url, const char* token, const char* key, const char* val, int ttl) noexcept;


int
etcd_keepalive(EtcdRsp* rsp, const char* base_url, const char* token, const char* lease_id) noexcept;


int
etcd_get_prefix(EtcdRsp* rsp, const char* base_url, const char* token, const char* prefix) noexcept;


} // namespace typhon::utils


#endif // __TYPHON_UTILS_ETCD_HPP__