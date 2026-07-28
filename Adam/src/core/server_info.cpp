#include "core/server_info.hpp"


int
adam::core::ServerInfo::from_yaml(const YAML::Node& root) noexcept {
    if (!root["id"] || !root["timeout"] || !root["name"] || !root["host"] || !root["desc"] || !root["protocol"]) {
        return -1;
    }

    id         = root["id"].as<uint32_t>();
    timeout    = root["timeout"].as<uint64_t>() * 1000;
    protocol   = root["protocol"].as<std::string>();
    name       = root["name"].as<std::string>();
    host       = root["host"].as<std::string>();
    desc       = root["desc"].as<std::string>();
    start_time = ::time(nullptr);
    router     = root["router"] ? root["router"].as<bool>() : false;

    if (id == 0) {
        return -1;
    }

    if (timeout == 0) {
        return -1;
    }

    if (protocol != "kcp" && protocol != "tcp" && protocol != "udp" && protocol != "ws") {
        return -1;
    }

    if (name.empty()) {
        return -1;
    }

    if (host.empty()) {
        return -1;
    }

    if (desc.empty()) {
        return -1;
    }

    auto n = std::thread::hardware_concurrency();
    nthreads = n > 2 ? n - 2 : 1;

    // id 十六进制: 高 2 字节 = 类型, 低 2 字节 = 序号(如 0x10000000 → 类型 1000 / 序号 0000)
    key = std::format("/{}/{:08X}", name, id);
    val = to_json();

    return 0;
}
