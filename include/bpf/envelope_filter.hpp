#ifndef __TYPHON_BPF_ENVELOPE_FILTER_HPP__
#define __TYPHON_BPF_ENVELOPE_FILTER_HPP__


#include <cstdint>


struct bpf_object;


namespace typhon::bpf {


/**
 * @brief 加载并管理 envelope.bpf.o (XDP SipHash MAC 过滤).
 *
 * 把 src/bpf/envelope.bpf.c 编译出的 ELF 加载进内核, attach 到指定网卡,
 * 在 wire 入口校验 UDP envelope MAC (前 8 字节 SipHash tag).
 *
 * **生命周期**:
 *   1. `init(obj_path, target_port, key)`  — 加载 + 写 target_port 到 .rodata + 写 key 到 map slot 0
 *   2. `attach(ifname)`                     — 挂载到网卡 (XDP_DRV_MODE first, fallback XDP_SKB_MODE)
 *   3. (运行中) `rotate_key(new_key)`      — 旧 key 自动搬到 slot 1, 新 key 写 slot 0
 *   4. 析构 或 `detach()`                   — 卸载 + close
 *
 * **kernel 要求**: Linux >= 5.17 (envelope.bpf.c 用了 bpf_loop helper).
 *
 * @warning 线程不安全. attach / detach / rotate_key 不能并发调用; 通常在
 *          `typhon::Server::run()` 启动期完成 init+attach, key rotate 由独立
 *          的定时线程串行触发.
 */
class EnvelopeFilter {
    EnvelopeFilter(const EnvelopeFilter&) = delete;
    EnvelopeFilter& operator=(const EnvelopeFilter&) = delete;
    EnvelopeFilter(EnvelopeFilter&&) = delete;
    EnvelopeFilter& operator=(EnvelopeFilter&&) = delete;


public:
    /// SipHash key 长度. 与 utils::SIPHASH_KEY_LEN 同, 这里冗余声明避免 include 链.
    static constexpr int KEY_LEN = 16;


    explicit
    EnvelopeFilter() noexcept = default;


    ~EnvelopeFilter() noexcept;


    /**
     * @brief 加载 BPF ELF 对象到内核, 写入 target_port + key.
     *
     * @param obj_path     编译产物路径, 通常 "build/bpf/envelope.bpf.o"
     * @param target_port  本机监听 UDP 端口 (例如 5555). XDP 只对该 dst port
     *                     做 MAC 校验, 其他端口 XDP_PASS 放过.
     *                     传 0 表示对所有 UDP 包做校验 (不推荐, 影响无关流量).
     * @param key          16 字节 SipHash key, 与 sender 端 kcp::Conf::shkey()
     *                     必须一致, 否则两端 tag 不匹配, 包全被 drop.
     * @return  0  成功
     * @return <0  错误 (-EINVAL / libbpf 错误码)
     */
    int
    init(const char* obj_path, uint16_t target_port, const uint8_t key[KEY_LEN]) noexcept;


    /**
     * @brief 把 XDP 程序 attach 到指定网卡.
     *
     * 先试 XDP_FLAGS_DRV_MODE (网卡驱动 native, 性能最好);
     * 失败则 fallback XDP_FLAGS_SKB_MODE (generic, 内核协议栈处理后再丢, 较慢但兼容).
     *
     * @param ifname 网卡名 (例如 "eth0" / "ens3" / "lo")
     * @return  0  成功
     * @return <0  错误
     */
    int
    attach(const char* ifname) noexcept;


    /**
     * @brief 卸载 XDP 程序. 调用方负责保证 attach / detach 配对.
     */
    int
    detach() noexcept;


    /**
     * @brief 切换 SipHash key.
     *
     * 流程:
     *   - 读 slot 0 (current) 当前 key
     *   - 写到 slot 1 (previous)
     *   - 把 new_key 写到 slot 0
     *
     * envelope.bpf.c 的校验逻辑会 current 不过自动尝试 previous, 所以
     * **rotate 期间客户端用旧 key 发的包仍然通过**, 不需要停服。
     *
     * @param new_key 新的 16 字节 key
     * @return  0  成功
     * @return <0  错误
     */
    int
    rotate_key(const uint8_t new_key[KEY_LEN]) noexcept;


private:
    ::bpf_object* obj_         { nullptr };
    int           prog_fd_     { -1 };      ///< XDP program fd
    int           key_map_fd_  { -1 };      ///< envelope_key map fd
    int           if_index_    { -1 };      ///< attached interface index, -1 表示未 attach
    unsigned int  xdp_flags_   { 0 };       ///< 实际 attach 用的模式 (用于 detach)
};


} // namespace typhon::bpf


#endif // __TYPHON_BPF_ENVELOPE_FILTER_HPP__