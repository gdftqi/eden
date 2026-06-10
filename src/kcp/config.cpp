#include "kcp/config.hpp"
#include "utils/log.hpp"
#include "core/error.hpp"


int
typhon::kcp::Conf::load(const YAML::Node& node) {
    if (!node["id"]) {
        xERROR("id is invalid");
        return xERR_PARAM;
    }
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

    if (node["siphash"]) {
        auto tmp = node["siphash"].as<std::string>();
        if (tmp.length() != sizeof(SipKey)) {
            xERROR("无效的 siphash, 长度必需为 16 个字符");
            return xERR_PARAM;
        }
        ::memcpy(siphash_, (uint8_t*)tmp.data(), sizeof(SipKey));
    }

    return xOK;
}