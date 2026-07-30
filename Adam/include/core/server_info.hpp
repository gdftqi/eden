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


    typedef std::bitset<PID_MAX> PIDSet;


    /**
     * @brief 设置 消息ID
     */
    void
    pid_set(uint16_t pid) noexcept {
        pids.set(pid);
        val = to_json();
    }


    /**
     * @brief 获取服务类型
     */
    uint32_t
    get_type() const noexcept {
        return id >> 16;
    }


    /**
     * @brief 获取服务序号
     */
    uint32_t
    get_seq() const noexcept {
        return 0x0000FFFF & id;
    }


    /** 
     * @brief 转 json 字符串
     */
    std::string
    to_json() const noexcept;


    /**
     * @brief 从 json 格式构建对象
     */
    int
    from_json(const std::string& json) noexcept;


    /**
     * @brief 从 yaml 格式构建对象
     */
    int
    from_yaml(const YAML::Node& root) noexcept;


    uint32_t    id          { 0 };     // 服务IDs
    uint32_t    nthreads    { 0 };     // 工作线程数
    uint64_t    timeout     { 0 };     // 超时值
    std::string protocol;              // 协议
    std::string name;                  // 服务名称
    std::string host;                  // 监听地址
    std::string desc;                  // 描述信息
    ::time_t    start_time;            // 启动时间
    bool        router      { false }; // 本服务是否为终端路由服务(网关按此挑选 ENT 的收件人)
    PIDSet      pids;                  // PID位
    std::string key;                   // 用于注册 etcd 的 keys
    std::string val;                   // 用于注册 etcd 的 value


    ServerInfo() = default;
    ~ServerInfo() = default;
    ServerInfo(const ServerInfo&) = delete;
    ServerInfo& operator=(const ServerInfo&) = delete;
    ServerInfo(ServerInfo&&) = delete;
    ServerInfo& operator=(ServerInfo&&) = delete;
}; // struct ServerInfo;

    
} // namespace adam::core


#endif // __ADAM_CORE_SERVER_HPP__
