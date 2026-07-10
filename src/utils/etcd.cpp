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
typhon::utils::etcd_auth(EtcdRsp* rsp, const char* base_url, const char* user, const char* pwd) noexcept {
    ASSERT(rsp != nullptr, "rsp 不能为 null");
    rsp->reset();
    
    int res = http_post(
        rsp, 
        std::format("{}{}", base_url, "/v3/auth/authenticate").c_str(), 
        std::format("{{\"name\":\"{}\",\"password\":\"{}\"}}", user, pwd).c_str(), 
        nullptr, nullptr, 0
    );

    if (res != 0) {
        return res;
    }

    return -rsp->code;
}


int
typhon::utils::etcd_grant_put(EtcdRsp* rsp, const char* base_url, const char* token, const char* key, const char* val, int ttl) noexcept {
    ASSERT(rsp != nullptr, "rsp 不能为 null");
    rsp->reset();

    const char* hk[] = {"Authorization"};
    const char* hv[] = { token };

    int res = http_post(
        rsp,
        std::format("{}{}", base_url, "/v3/lease/grant").c_str(),
        std::format("{{\"TTL\":\"{}\"}}", ttl).c_str(),
        hk, hv, 1
    );

    if (res != 0) {
        return res;
    }

    if (rsp->code != 0) {
        return -rsp->code;
    }

    if (rsp->id.empty()) {
        return -1;
    }

    std::string sk, sv, leaseID = rsp->id;
    utils::base64_encode(sk, (const uint8_t*)key, ::strlen(key));
    utils::base64_encode(sv, (const uint8_t*)val, ::strlen(val));

    res = http_post(
        rsp,
        std::format("{}{}", base_url, "/v3/kv/put").c_str(),
        std::format("{{\"key\":\"{}\",\"value\":\"{}\",\"lease\":\"{}\"}}", sk, sv, leaseID).c_str(),
        hk, hv, 1
    );

    if (res != 0) {
        return res;
    }

    return -rsp->code;
}


int
typhon::utils::etcd_keepalive(EtcdRsp* rsp, const char* base_url, const char* token, const char* lease_id) noexcept {
    ASSERT(rsp != nullptr, "rsp 不能为 null");
    rsp->reset();

    const char* hk[] = {"Authorization"};
    const char* hv[] = { token };

    int res = http_post(
        rsp,
        std::format("{}{}", base_url, "/v3/lease/keepalive").c_str(),
        std::format("{{\"ID\":\"{}\"}}", lease_id).c_str(),
        hk, hv, 1
    );

    if (res != 0) {
        return res;
    }

    if (rsp->ttl.empty() || rsp->ttl == "0") {
        return -1;
    }

    return -rsp->code;
}


int
typhon::utils::etcd_get_prefix(EtcdRsp* rsp, const char* base_url, const char* token, const char* prefix) noexcept {
    ASSERT(rsp != nullptr, "rsp 不能为 null");
    rsp->reset();

    const char* hk[] = {"Authorization"};
    const char* hv[] = { token };

    if (prefix == nullptr || ::strlen(prefix) == 0) {
        return -1;
    }
    
    std::string end = prefix;
    end.back()++;

    std::string sk, sre;
    utils::base64_encode(sk, (const uint8_t*)prefix, ::strlen(prefix));
    utils::base64_encode(sre, (const uint8_t*)end.c_str(), end.length());

    int res = http_post(
        rsp,
        std::format("{}{}", base_url, "/v3/kv/range").c_str(),
        std::format("{{\"key\":\"{}\",\"range_end\":\"{}\"}}", sk, sre).c_str(),
        hk, hv, 1
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