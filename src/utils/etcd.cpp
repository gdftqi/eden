#include "utils/etcd.hpp"
#include "utils/http_client.hpp"
#include "utils/string_ex.hpp"


int
typhon::utils::EtcdRsp::deserialize(EtcdRsp* out, const std::string& json) noexcept {
    ASSERT(out != nullptr, "out 不能为 null");

    simdjson::ondemand::parser parser;
    auto j = simdjson::padded_string(json);
    auto root = parser.iterate(j);

    if (root.error() != simdjson::SUCCESS) {
        return -1;
    }

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

    if (root["result"].has_value()) {
        auto r = root["result"];
        if (r["ID"].has_value()) {
            out->id = std::string(r["ID"].get_string().value_unsafe());
        }

        if (r["TTL"].has_value()) {
            out->ttl = std::string(r["TTL"].get_string().value_unsafe());
        }
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

    if (root["kvs"].has_value()) {
        auto kvs = root["kvs"].get_array();
        for (auto&& kv: kvs) {
            out->kvs.emplace_back(std::make_pair(
                kv["key"].has_value() ? std::string(kv["key"].get_string().value_unsafe()) : "",
                kv["value"].has_value() ? std::string(kv["value"].get_string().value_unsafe()) : ""
            ));
        }
    }

    return 0;
}


int
typhon::utils::etcd_auth(EtcdRsp* rsp, const char* url, const char* user, const char* pwd) noexcept {
    ASSERT(rsp != nullptr && url != nullptr && user != nullptr && pwd != nullptr, "入参错误");
    rsp->reset();
    
    int res = Http::instance()->post(
        rsp, 
        std::format("{}{}", url, "/v3/auth/authenticate").c_str(), 
        std::format("{{\"name\":\"{}\",\"password\":\"{}\"}}", user, pwd).c_str()
    );

    if (res != 0) {
        return res;
    }

    if (rsp->code != 0) {
        xERROR("etcd_auth failed: {}", rsp->message);
        return -rsp->code;
    }

    return 0;
}


int
typhon::utils::etcd_grant(EtcdRsp* rsp, const char* url, int ttl) noexcept {
    ASSERT(rsp != nullptr && url != nullptr && ttl > 0, "入参错误");
    rsp->reset();

    int res = Http::instance()->post(
        rsp,
        std::format("{}{}", url, "/v3/lease/grant").c_str(),
        std::format("{{\"TTL\":\"{}\"}}", ttl).c_str()
    );

    if (res != 0) {
        return res;
    }

    if (rsp->code != 0) {
        xERROR("etcd_grant failed: {}", rsp->message);
        return -rsp->code;
    }

    if (rsp->id.empty()) {
        return -1;
    }

    return 0;
}


int
typhon::utils::etcd_put(EtcdRsp* rsp, const char* url, const char* token, const char* key, const char* val, const char* lease) noexcept {
    ASSERT(rsp != nullptr && url != nullptr && token != nullptr && key != nullptr && val != nullptr && lease != nullptr, "入参错误");

    auto k = utils::base64_encode(key);
    auto v = utils::base64_encode(val);

    int res = Http::instance()->post(
        rsp,
        std::format("{}{}", url, "/v3/kv/put").c_str(),
        std::format("{{\"key\":\"{}\",\"value\":\"{}\",\"lease\":\"{}\"}}", k, v, lease).c_str(),
        { std::make_pair("Authorization", token) }
    );

    if (res != 0) {
        return res;
    }

    if (rsp->code != 0) {
        xERROR("etcd_put failed: {}", rsp->message);
        return -rsp->code;
    }

    return 0;
}


int
typhon::utils::etcd_keepalive(EtcdRsp* rsp, const char* url, const char* token, const char* lease) noexcept {
    ASSERT(rsp != nullptr && url != nullptr && token != nullptr && lease != nullptr, "入参错误");
    rsp->reset();

    int res = Http::instance()->post(
        rsp,
        std::format("{}{}", url, "/v3/lease/keepalive").c_str(),
        std::format("{{\"ID\":\"{}\"}}", lease).c_str(),
        { std::make_pair("Authorization", token) }
    );

    if (res != 0) {
        return res;
    }

    if (rsp->ttl.empty() || rsp->ttl == "0") {
        return -1;
    }

    if (rsp->code != 0) {
        xERROR("etcd_keepalive failed: {}", rsp->message);
        return -rsp->code;
    }

    return 0;
}


int
typhon::utils::etcd_delete(EtcdRsp* rsp, const char* url, const char* token, const char* key) noexcept {
    ASSERT(rsp != nullptr && url != nullptr && token != nullptr && key != nullptr, "入参错误");
    rsp->reset();

    std::string sk;
    utils::base64_encode(sk, (const uint8_t*)key, ::strlen(key));

    int res = Http::instance()->post(
        rsp,
        std::format("{}{}", url, "/v3/kv/deleterange").c_str(),
        std::format("{{\"key\":\"{}\"}}", sk).c_str(),
        { std::make_pair("Authorization", token) }
    );

    if (res != 0) {
        return res;
    }

    if (rsp->code != 0) {
        xERROR("etcd_delete failed: {}", rsp->message);
        return -rsp->code;
    }

    return 0;
}


int
typhon::utils::etcd_get_prefix(EtcdRsp* rsp, const char* url, const char* token, const char* prefix) noexcept {
    ASSERT(rsp != nullptr && url != nullptr && token != nullptr && prefix != nullptr, "入参错误");
    rsp->reset();

    if (prefix == nullptr || ::strlen(prefix) == 0) {
        return -1;
    }
    
    std::string end = prefix;
    end.back()++;

    std::string sk, sre;
    utils::base64_encode(sk, (const uint8_t*)prefix, ::strlen(prefix));
    utils::base64_encode(sre, (const uint8_t*)end.c_str(), end.length());

    int res = Http::instance()->post(
        rsp,
        std::format("{}{}", url, "/v3/kv/range").c_str(),
        std::format("{{\"key\":\"{}\",\"range_end\":\"{}\"}}", sk, sre).c_str(),
        { std::make_pair("Authorization", token) }
    );
    if (res != 0) {
        return res;
    }

    if (rsp->code != 0) {
        return -rsp->code;
    }

    for (auto& [k, v]: rsp->kvs) {
        k = utils::base64_decode(k);
        v = utils::base64_decode(v);
    }

    return 0;
}