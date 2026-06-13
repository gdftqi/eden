#include "kcp/config.hpp"
#include "utils/log.hpp"
#include "core/error.hpp"
#include "utils/string_ex.hpp"


void
typhon::kcp::Conf::load(const YAML::Node& node) {
    ASSERT(node["id"] && node["siphash"] && node["x25519_pk"] && node["x25519_sk"] && node["ed25519_pub"], "缺少必要参数");

    id_ = node["id"].as<uint32_t>();

    if (node["sndbuf"]) {
        sndbuf_ = node["sndbuf"].as<int>();
    }

    if (node["rcvbuf"]) {
        rcvbuf_ = node["rcvbuf"].as<int>();
    }

    if (node["sndwnd"]) {
        sndwnd_ = node["sndwnd"].as<int>();
    }

    if (node["rcvwnd"]) {
        rcvwnd_ = node["rcvwnd"].as<int>();
    }

    if (node["nodelay"]) {
        nodelay_ = node["nodelay"].as<int>();
    }

    if (node["interval"]) {
        interval_ = node["interval"].as<int>();
    }

    if (node["resend"]) {
        resend_ = node["resend"].as<int>();
    }

    if (node["nc"]) {
        nc_ = node["nc"].as<int>();
    }

    if (node["timeout"]) {
        timeout_ = node["timeout"].as<uint32_t>();
    }

    auto tmp = node["siphash"].as<std::string>();
    ASSERT(tmp.length() == sizeof(SipKey), "无效的 siphash, 长度必需为 16 个字符");
    ::memcpy(siphash_, (uint8_t*)tmp.data(), sizeof(SipKey));

    tmp = node["x25519_pk"].as<std::string>();
    size_t len = sizeof(x25519_pk_);
    ASSERT(utils::base64_decode(tmp, x25519_pk_, &len) == xOK && len == sizeof(x25519_pk_), "无效的 x25519_pk");

    tmp = node["x25519_sk"].as<std::string>();
    len = sizeof(x25519_sk_);
    ASSERT(utils::base64_decode(tmp, x25519_sk_, &len) == xOK && len == sizeof(x25519_sk_), "无效的 x25519_sk"); 

    tmp = node["ed25519_pub"].as<std::string>();
    len = sizeof(ed25519_pub_);
    ASSERT(utils::base64_decode(tmp, ed25519_pub_, &len) == xOK && len == sizeof(ed25519_pub_), "无效的 ed25519_pub"); 
}