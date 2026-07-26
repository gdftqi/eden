#ifndef __ADAM_CORE_SERVER_HPP__
#define __ADAM_CORE_SERVER_HPP__


#include <cinttypes>
#include <string>
#include <bitset>
#include <format>
#include <simdjson.h>
#include <yaml-cpp/yaml.h>


namespace adam::core {


/**
 * @brief 服务信息
 */
struct ServerInfo {
    /**
     * @brief PID 是 16 位, 位图覆盖其全部取值空间(65536 bit = 8KB)
     */
    static constexpr size_t PID_MAX = 1 << 16;


    void
    pid_set(uint16_t pid) noexcept {
        pids.set(pid);
        val = to_json();
    }


    bool
    pid_has(uint16_t pid) const noexcept {
        return pids[pid];
    }


    uint32_t
    get_type() const noexcept {
        return id >> 16;
    }


    uint32_t
    get_seq() const noexcept {
        return 0x0000FFFF & id;
    }


    /** 
     * @brief 转 json 字符串
     */
    std::string
    to_json() const noexcept {
        // 位图 → 稀疏数组(注册时一次性, 6.5 万次位测试不值得优化)
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

        return std::format("{{\"id\":{},\"protocol\":\"{}\",\"name\":\"{}\",\"host\":\"{}\",\"desc\":\"{}\",\"start_time\":{},\"nthreads\":{},\"pkids\":[{}]}}", 
            id, protocol, name, host, desc, start_time, nthreads, ps);
    }


    /**
     * @brief 从 json 格式构建对象
     */
    int
    from_json(const std::string& json) noexcept {
        simdjson::ondemand::parser parser;
        auto j = simdjson::padded_string(json);
        auto doc = parser.iterate(j);

        if (doc["id"].has_value()) {
            id = doc["id"].get_uint32().value_unsafe();
        }

        if (doc["host"].has_value()) {
            host = std::string(doc["host"].get_string().value_unsafe());
        }

        if (doc["nthreads"].has_value()) {
            nthreads = doc["nthreads"].get_uint64().value_unsafe();
        }

        pids.reset();
        auto arr = doc["pkids"];
        if (arr.error() == simdjson::SUCCESS) {
            for (auto v : arr.get_array()) {
                pid_set((uint16_t)v.get_uint64().value_unsafe());
            }
        }

        return 0;
    }


    /**
     * @brief 从 yaml 格式构建对象
     */
    int
    from_yaml(const YAML::Node& root) noexcept;


    uint32_t    id        { 0 }; // 服务IDs
    uint32_t    nthreads  { 0 }; // 工作线程数
    uint64_t    timeout   { 0 }; // 超时值
    std::string protocol;        // 协议
    std::string name;            // 服务名称
    std::string host;            // 监听地址
    std::string desc;            // 描述信息
    ::time_t    start_time;      // 启动时间

    std::bitset<PID_MAX> pids;

    std::string key; // 用于注册 etcd 的 keys
    std::string val; // 用于注册 etcd 的 value

    ServerInfo() = default;
    ~ServerInfo() = default;
    ServerInfo(const ServerInfo&) = delete;
    ServerInfo& operator=(const ServerInfo&) = delete;
    ServerInfo(ServerInfo&&) = delete;
    ServerInfo& operator=(ServerInfo&&) = delete;
}; // struct ServerInfo;

    
} // namespace adam::core


#endif // __ADAM_CORE_SERVER_HPP__
