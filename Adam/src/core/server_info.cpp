#include "core/server_info.hpp"


std::string
adam::core::ServerInfo::to_json() const noexcept {
    std::string ps;
    for (size_t i = 0; i < PID_MAX; ++i) {
        if (!pids[i]) {
            continue;
        }

        if (!ps.empty()) {
            ps += ',';
        }
        ps += std::to_string(i);
    }

    return std::format("{{\"id\":{},\"protocol\":\"{}\",\"name\":\"{}\",\"host\":\"{}\",\"desc\":\"{}\",\"start_time\":{},\"router\":{},\"pids\":[{}]}}", 
        id, protocol, name, host, desc, start_time, router ? "true" : "false", ps);
}


int
adam::core::ServerInfo::from_json(const std::string& json) noexcept {
    simdjson::ondemand::parser parser;
    auto j = simdjson::padded_string(json);
    auto doc = parser.iterate(j);

    if (doc["id"].has_value()) {
        id = doc["id"].get_uint32().value_unsafe();
    }

    if (doc["host"].has_value()) {
        host = std::string(doc["host"].get_string().value_unsafe());
    }

    router = false;
    if (doc["router"].has_value()) {
        router = doc["router"].get_bool().value_unsafe();
    }

    pids.reset();
    if (doc["pids"].has_value()) {
        for (auto v : doc["pids"].get_array()) {
            pid_set((uint16_t)v.get_uint64().value_unsafe());
        }
    }

    return 0;
}


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
