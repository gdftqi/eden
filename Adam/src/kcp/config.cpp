#include "kcp/config.hpp"


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

    if (root["flame"]) {
        flame_ = root["flame"].as<bool>();
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

    if (root["siphash"]) {
        auto s = root["siphash"].as<std::string>();
        ASSERT(s.length() == sizeof(siphash_), "无效的 siphash");
        ::memcpy(siphash_, (uint8_t*)s.data(), sizeof(siphash_));
    }

    if (root["x25519_pk"]) {
        auto s = root["x25519_pk"].as<std::string>();
        size_t len = sizeof(x25519_pk_);
        ASSERT(utils::base64_decode(s, x25519_pk_, &len) == 0 && len == sizeof(x25519_pk_), "无效的 x25519_pk");
    }

    if (root["x25519_sk"]) {
        auto s = root["x25519_sk"].as<std::string>();
        size_t len = sizeof(x25519_sk_);
        ASSERT(utils::base64_decode(s, x25519_sk_, &len) == 0 && len == sizeof(x25519_sk_), "无效的 x25519_sk");
    }

    if (root["ed25519_pk"]) {
        auto s = root["ed25519_pk"].as<std::string>();
        size_t len = sizeof(ed25519_pk_);
        ASSERT(utils::base64_decode(s, ed25519_pk_, &len) == 0 && len == sizeof(ed25519_pk_), "无效的 ed25519_pk");
    }

    if (root["log_path"]) {
        log_path_ = root["log_path"].as<std::string>();
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
    ASSERT(!::sodium_is_zero(siphash_, sizeof(siphash_)), "siphash is invalid");
    ASSERT(!::sodium_is_zero(x25519_pk_, sizeof(x25519_pk_)), "x25519_pk is invalid");
    ASSERT(!::sodium_is_zero(x25519_sk_, sizeof(x25519_sk_)), "x25519_sk is invalid");
    ASSERT(!::sodium_is_zero(ed25519_pk_, sizeof(ed25519_pk_)), "ed25519_pk is invalid");
}