#ifndef __TYPHON_KCP_SESSION_HPP__
#define __TYPHON_KCP_SESSION_HPP__


#include "core/typhon.in.hpp"
#include "core/package.hpp"
#include "core/error.hpp"
#include "kcp/config.hpp"
#include "kcp/ikcp.h"


namespace typhon::kcp {


class Server;


/**
 * @brief Kcp Session 类
 */
class Session {
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;


public:
    typedef std::shared_ptr<Session> Ptr;


    /**
     * @brief 创建一个 Kcp 实例(shared_ptr,便于跨 epoll cycle / sque_ 持有)。
     * @param conv   KCP conv ID,客户端 / 服务端必须一致
     * @param server 所属 KcpServer
     */
    static Ptr
    create(uint32_t conv, Server* server, const void* addr, socklen_t addrlen) noexcept {
        return std::make_shared<Session>(conv, server, addr, addrlen);
    }


    /**
     * @brief 获取 conv
     */
    static uint32_t
    getconv(const void* data, int len) noexcept {
        return len < 4 ? 0 : ::ikcp_getconv(data);
    }


    /**
     * @brief 构造函数
     */
    explicit
    Session(uint32_t conv, Server* server, const void* addr, socklen_t addrlen) noexcept;


    /**
     * @brief 析构函数
     */
    ~Session() noexcept {
        if (kcp_) {
            ::ikcp_release(kcp_);
        }
    }


    /**
     * @brief kcp conv
     */
    uint32_t
    conv() const noexcept {
        return kcp_->conv;
    }


    std::string
    to_string() const noexcept {
        return desc_;
    }
    

    /**
     * @brief 所属服务
     */
    Server*
    server() noexcept {
        return server_;
    }


    /**
     * @brief Kcp 对端地址
     */
    ::sockaddr_storage*
    addr() noexcept {
        return &addr_;
    }


    /**
     * @brief Kcp 对端地址长度
     */
    ::socklen_t
    addrlen() const noexcept {
        return addrlen_;
    }


    /**
     * @brief 对端地址(字符串)
     */
    std::string
    remote_addr() const noexcept {
        return core::sockaddr_to_string((sockaddr*)&addr_);
    }


    uint32_t
    remote_addr_u32() const noexcept {
        return core::sockaddr_to_u32((sockaddr_in*)&addr_);
    }


    /**
     * @brief 检测超时
     */
    bool
    check_timeout(uint64_t tnow) const noexcept {
        return tnow - last_recv_ms_ > (uint64_t)Conf::instance()->timeout();
    }


    /**
     * @brief 推动 KCP 内部状态机:超时重传、发 ACK、flush 待发数据。
     *        必须按 ikcp_nodelay() 设的 interval 周期调 —— 不调用 KCP 不会推进,
     *        重传和 ACK 都会停。
     * @param current 单调递增的毫秒时间戳(KCP 只用 delta,起点无所谓,但所有 KCP
     *                调用必须用同一时间源)。典型用 CLOCK_MONOTONIC 转 ms。
     */
    void
    update(uint64_t current) noexcept {
        // ikcp 协议层用 uint32 ms (内部 _itimediff 是 wrap-safe), cast 后正确.
        ::ikcp_update(kcp_, (uint32_t)current);
    }


    /**
     * @brief 把对端发来的一段 UDP payload 喂给 KCP 状态机:解 KCP 头、放进 rcv_queue、
     *        触发 ACK / 重传。喂完之后才能用 recv_pk() 取出完整应用层消息。
     *        成功时同步记录对端地址,后续 output 回调通过 addr() 取到。
     * @param data    UDP payload 起始
     * @param len     payload 字节数
     * @param addr    对端 sockaddr(从 recvmmsg 拿到的 msg_hdr.msg_name)
     * @param addrlen addr 长度
     * @return  xOK 成功; xERR_KCP_* 错误(见 core/error.hpp)
     */
    int
    input(const void* data, long len, const void* addr, socklen_t addrlen) noexcept {
        int res = ::ikcp_input(kcp_, (const char*)data, len);
        if (res == 0) {
            ::memcpy(&addr_, addr, addrlen);
            addrlen_ = addrlen;
        }
        return core::from_ikcp_input(res);
    }


    /**
     * @brief 设置 KCP 出网卡回调。KCP 内部 flush 时(ACK / 重传 / 新 segment)
     *        通过这个回调把字节交给上层,上层负责真正 sendto。
     *        typhon 里 output 由 KcpServer 统一实现,把字节排进 sque_,
     *        update() 用 sendmmsg 批量发出。
     * @param output 回调函数指针。user 参数是 ctor 里传入的 this(Kcp 实例)
     */
    void
    set_output(int (*output)(const char *buf, int len, struct IKCPCB *kcp, void *user)) noexcept {
        ::ikcp_setoutput(kcp_, output);
    }


    /**
     * @brief 从 KCP 队列读出一条完整 Package,做协议自检 + 单调性幂等校验,
     *        并刷新 last_recv_ms_(用于 session 超时判定)。
     *        相当于 recv() 之上加一层应用协议层处理。
     *
     * @param[out] pkg 解析成功时指向 buf 起始(host 字节序,可直接访问字段)
     * @param      buf 接收缓冲;长度应 >= PKG_MAX_LEN,否则会触发 ikcp_recv 的 -3
     * @param      len buf 长度
     * @param      now 当前 tnow_(用于刷新 last_recv_ms_)
     *
     * @return  xOK    成功(包长度在 *pk 的 len() 里)
     *          xDUP   幂等重复包, 跳过(可继续 recv 下一条)
     *          xAGAIN rcv_queue 空 / 无完整包, 当前没有更多消息
     *          xERR_KCP_BUFSMALL          buf 太小, 放大后重试
     *          xERR_PKT_LEN/ID/IDEM/DST/DEC  协议自检失败(见 core/error.hpp)
     */
    int
    recv(core::PK<core::Host>* pk, uint8_t* buf, int len, uint64_t now) noexcept;


    int
    send(core::PK<core::Host> &pk) noexcept;


private:
    /**
     * @brief 下一个发送幂等
     */
    uint32_t
    next_snd_seq() noexcept {
        return ++snd_seq_;
    }


    /**
     * @brief 从 KCP 接收队列里取出一条完整的应用层消息(对应一次 ikcp_send)。
     *        KCP 是面向消息的:返回的就是对端一次 send 写入的整体,**不会半包也不会粘包**。
     * @param buf 输出缓冲(必须能装下下一条消息,否则返回 -3)
     * @param len buf 的可写长度(字节数)
     * @return > 0  实际拷出的字节数(即整条消息的长度)
     * @return  -1  rcv_queue 为空,当前没有完整消息(典型情况,继续等下一次 input)
     * @return  -2  peek 失败,KCP 内部状态异常(正常路径不应出现)
     * @return  -3  buf 太小,装不下下一条消息;调用方应放大 buf 后重试,
     *              否则该消息会一直卡在 rcv_queue
     */
    int
    recv(uint8_t* buf, int len) noexcept {
        return ::ikcp_recv(kcp_, (char*)buf, len);
    }


    /**
     * @brief 把一条完整的应用层消息追加到 KCP 的发送队列。**不会立即出网卡** ——
     *        真正出网卡由 update() 的周期 flush(或手动 flush())触发,内部会按
     *        MTU 自动分片、ACK、超时重传。
     *        KCP 是面向消息的:这一次 send 进去的整段 buf,对端 recv 会作为**一条
     *        完整消息**收到,不会被切碎也不会跟下一条粘在一起。
     * @param buf 待发送的字节流
     * @param len buf 的字节数,必须 >= 0;不可超过 (rcvwnd × MSS)(分片数受接收窗约束)
     * @return   0  成功入队,等待 flush 出网卡
     * @return  -1  len < 0
     * @return  -2  消息过大,分片数 >= 接收窗口,无法入队;
     *              调用方需要缩短单条消息长度,或扩大 rcvwnd
     */
    int
    send(const uint8_t* buf, int len) noexcept {
        return ::ikcp_send(kcp_, (const char*)buf, len);
    }


    Server*            server_         { nullptr };
    uint64_t           last_recv_ms_   { 0 };
    uint32_t           snd_seq_       { 0 };
    uint32_t           rcv_req_       { 0 };
    ::ikcpcb*          kcp_            { nullptr };
    ::sockaddr_storage addr_           {};
    ::socklen_t        addrlen_        { sizeof(addr_) };
    std::string        desc_;
}; // class Kcp;


}; // namespace typhon::core;


#endif // __TYPHON_KCP_SESSION_HPP__