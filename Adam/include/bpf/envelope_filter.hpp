#ifndef __ADAM_BPF_ENVELOPE_FILTER_HPP__
#define __ADAM_BPF_ENVELOPE_FILTER_HPP__


#include <cstdint>
#include "utils/cryptor.hpp"


struct bpf_object;


namespace adam::bpf {


/**
 * @brief 加载并管理 envelope.bpf.o (XDP SipHash MAC 过滤).
 *
 * 把 src/bpf/envelope.bpf.c 编译出的 ELF 加载进内核, attach 到指定网卡.
 *
 * 在 wire 入口校验 UDP envelope MAC (前 8 字节 SipHash tag).
 *
 * @note kernel 要求: Linux >= 5.17.
 *
 * @warning 线程不安全
 */
class EnvelopeFilter {
public:
    typedef uint8_t SipHashKey[utils::SIPHASH_KEY_LEN];


    explicit
    EnvelopeFilter() noexcept
    {}


    ~EnvelopeFilter() noexcept;


    /**
     * @brief 加载 BPF ELF 对象到内核, 写入 udp_port + key.
     *
     * 仅对UDP有效
     *
     * @param obj_path  envelope.bpf.o 对象文件路径
     * @param udp_port  监听 UDP 端口
     * @param key       16 字节 SipHash key
     *
     * @return 成功返回 0, 否则表示错误(-EINVAL / libbpf 错误码)
     */
    int
    init(const char* obj_path, uint16_t udp_port,
         const SipHashKey* keys, int keys_count, int newsess_max) noexcept;


    /**
     * @brief 把 XDP 程序 attach 到指定网卡.
     *
     * 先试 XDP_FLAGS_DRV_MODE (网卡驱动 native);
     * 失败则 fallback XDP_FLAGS_SKB_MODE (generic, 内核协议栈).
     *
     * @param ifname 网卡名 (例如 "eth0" / "ens3" / "lo")
     * @return 成功返回 0, 否则表示错误
     */
    int
    attach(const char* ifname) noexcept;


    /**
     * @brief 卸载 XDP 程序. 调用方负责保证 attach / detach 配对.
     */
    int
    detach() noexcept;


    /**
     * @brief 把 conv 登记为"活跃会话". XDP 据此区分已建立的会话和新会话尝试 --
     * 前者一律放行(避免误伤 NAT 后面共用出口 IP 的一片玩家), 后者按源 IP 限速.
     *
     * @note 未加载 BPF(配置里没配 envelope_bpf_path)时是空操作, 安全属性不依赖它.
     * 登记失败只记日志不中断: 最坏结果是该会话的包被当成新会话尝试限速.
     */
    void
    conv_add(uint32_t conv) noexcept;


    /**
     * @brief 摘除会话时注销 conv. 漏掉会让表项一直占着, 直到进程重启.
     */
    void
    conv_del(uint32_t conv) noexcept;


private:
    ::bpf_object* obj_         { nullptr };
    int           prog_fd_     { -1 };      // XDP program fd
    int           key_map_fd_  { -1 };      // envelope_key map fd
    int conv_map_fd_ { -1 }; // active_conv map fd
    int           if_index_    { -1 };      // attached interface index, -1 表示未 attach
    unsigned int  xdp_flags_   { 0 };       // 实际 attach 用的模式 (用于 detach)


    EnvelopeFilter(const EnvelopeFilter&) = delete;
    EnvelopeFilter& operator=(const EnvelopeFilter&) = delete;
    EnvelopeFilter(EnvelopeFilter&&) = delete;
    EnvelopeFilter& operator=(EnvelopeFilter&&) = delete;
}; // class EnvelopeFilter;


} // namespace adam::bpf;


#endif // __ADAM_BPF_ENVELOPE_FILTER_HPP__
