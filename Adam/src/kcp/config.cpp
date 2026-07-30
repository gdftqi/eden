#include "kcp/config.hpp"
#include "utils/string_ex.hpp"


void
adam::kcp::Conf::load_from_file(const char* fname) noexcept {
    auto root = YAML::LoadFile(fname);
    if (!root["server"] || server_.from_yaml(root["server"]) < 0) {
        xFATAL("config.server is invalid");
    }

    if (!root["etcd"] || etcd_.from_yaml(root["etcd"]) < 0) {
        xFATAL("config.etcd is invalid");
    }

    if (root["ifname"]) {
        ifname_ = root["ifname"].as<std::string>();
    }

    if (root["kcp_bpf_path"]) {
        kcp_bpf_path_ = root["kcp_bpf_path"].as<std::string>();
    }

    if (root["envelope_bpf_path"]) {
        envelope_bpf_path_ = root["envelope_bpf_path"].as<std::string>();
    }
        
    if (root["sndbuf"]) {
        sndbuf_ = root["sndbuf"].as<int>();
    }

    if (root["rcvbuf"]) {
        rcvbuf_ = root["rcvbuf"].as<int>();
    }
        
    if (root["sndwnd"]) {
        sndwnd_ = root["sndwnd"].as<int>();
    }

    if (root["rcvwnd"]) {
       rcvwnd_ = root["rcvwnd"].as<int>();
    }

    if (root["nodelay"]) {
        nodelay_ = root["nodelay"].as<int>();
    }

    if (root["interval"]) {
        interval_ = root["interval"].as<int>();
    }

    if (root["resend"]) {
        resend_ = root["resend"].as<int>();
    }

    if (root["nc"]) {
        nc_ = root["nc"].as<int>();
    }

    if (root["ed25519_pk"]) {
        auto s = root["ed25519_pk"].as<std::string>();
        size_t len = sizeof(ed25519_pk_);
        ASSERT(utils::base64_decode(s, ed25519_pk_, &len) == 0 && len == sizeof(ed25519_pk_), "无效的 ed25519_pk");
    }

    if (root["log_path"]) {
        log_path_ = root["log_path"].as<std::string>();
    }

    if (root["prof_path"]) {
        prof_path_ = root["prof_path"].as<std::string>();
    }

    if (root["nsiphash"]) {
        nsiphash_ = root["nsiphash"].as<int>();
    }

    // 全部校验在 load 内完成 (不再单独提供 check)
    ASSERT(sndbuf_ > 0, "sndbuf is invalid");
    ASSERT(rcvbuf_ > 0, "rcvbuf is invalid");
    ASSERT(sndwnd_ > 0 && sndwnd_ <= 128, "sndwnd is invalid");
    ASSERT(rcvwnd_ > 0 && rcvwnd_ <= 128, "rcvwnd is invalid");
    ASSERT(nodelay_ == 0 || nodelay_ == 1, "nodelay is invalid");
    ASSERT(interval_ >= 10, "interval is invalid");
    ASSERT(resend_ > 0, "resend is invalid");
    ASSERT(nc_ == 0 || nc_ == 1, "nc is invalid");
    ASSERT(!::sodium_is_zero(ed25519_pk_, sizeof(ed25519_pk_)), "ed25519_pk is invalid");

    ASSERT(utils::x25519_keygen(x25519_pk_, x25519_sk_) == xOK, "生成 x25519 密钥对失败");
    ASSERT(!::sodium_is_zero(x25519_pk_, sizeof(x25519_pk_)), "x25519_pk is invalid");
    ASSERT(!::sodium_is_zero(x25519_sk_, sizeof(x25519_sk_)), "x25519_sk is invalid");

    ASSERT(nsiphash_ > 0 && (nsiphash_ & (nsiphash_ - 1)) == 0 && nsiphash_ <= 256, "nsiphash 必须是 2 的幂(XDP 侧用 conv & (n-1) 取下标, 避免除法): {}", nsiphash_);

    siphashs_ = (SipHashKey*)::mi_malloc(sizeof(SipHashKey) * nsiphash_);
    ASSERT(siphashs_ != nullptr, "siphash keys 分配失败");

    for (int i = 0; i < nsiphash_; ++i) {
        ::randombytes_buf(siphashs_[i], sizeof(SipHashKey));
    }

    std::string keys;
    keys.reserve((size_t)nsiphash_ * 28);

    for (int i = 0; i < nsiphash_; ++i) {
        std::string b64;
        utils::base64_encode(b64, siphashs_[i], sizeof(SipHashKey));

        if (i > 0) {
            keys += ',';
        }

        keys += '"';
        keys += b64;
        keys += '"';
    }

    std::string pk_b64;
    utils::base64_encode(pk_b64, x25519_pk_, sizeof(x25519_pk_));

    json_ = std::format(
        "{{\"server\":{},\"x25519_pk\":\"{}\",\"count\":{},\"keys\":[{}]}}", server_.to_json(), pk_b64, nsiphash_, keys);
}
