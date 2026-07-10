#include "utils/etcd.hpp"
#include "utils/http_client.hpp"
#include "utils/string_ex.hpp"


int
typhon::utils::etcd_auth(EtcdRsp* rsp, const char* base_url, const char* user, const char* pwd) noexcept {
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