#ifndef __TYPHON_KCP_SESSION_HPP__
#define __TYPHON_KCP_SESSION_HPP__


#include "core/typhon.in.hpp"
#include "core/package.hpp"
#include "core/error.hpp"
#include "kcp/config.hpp"
#include "kcp/xkcp.h"


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
     * @brief 构造函数
     */
    explicit
    Session(uint32_t conv, Server* server, const void* addr, socklen_t addrlen) noexcept;


    /**
     * @brief 析构函数
     */
    ~Session() noexcept {
        if (kcp_) {
            ::xkcp_release(kcp_);
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


    bool
    authed() const noexcept {
        return kcp_->auth > 0;
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
     * @brief 推动 KCP 内部状态机:超时重传、发 ACK、flush 待发数据。
     *        必须按 ikcp_nodelay() 设的 interval 周期调 —— 不调用 KCP 不会推进,
     */
    int
    update(uint64_t current) noexcept {
        return ::xkcp_update(kcp_, (uint32_t)current);
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
        int res = ::xkcp_input(kcp_, (uint8_t*)data, len);
        if (res == 0) {
            ::memcpy(&addr_, addr, addrlen);
            addrlen_ = addrlen;
        }
        return core::from_ikcp_input(res);
    }


    /**
     * @brief 从 xkcp 队列读出一条完整 Package(整条消息 AEAD 已在 xkcp_recv 内解好),
     *        做协议自检(id / seq / dst_id 必须非 0)。
     *
     * @param[out] pk  解析成功时指向 buf 起始(host 字节序, 可直接访问字段)
     * @param      buf 接收缓冲; 长度应 >= PKG_MAX_LEN, 否则触发 xkcp_recv 的 -3
     * @param      len buf 长度
     *
     * @return  xOK    成功(包长度在 *pk 的 len() 里)
     *          xAGAIN rcv_queue 空 / 无完整包, 当前没有更多消息
     *          xERR_KCP_BUFSMALL                  buf 太小, 放大后重试
     *          xERR_PK_LEN/PK_ID/PKT_SEQ/PKT_DST  协议自检失败(见 core/error.hpp)
     */
    int
    recv(core::PK<core::Host>* pk, uint8_t* buf, int len) noexcept;


    int
    send(core::PK<core::Host> &pk) noexcept;


private:
    Server*            server_       { nullptr };
    ::xkcpcb*          kcp_          { nullptr };
    ::sockaddr_storage addr_         {};
    ::socklen_t        addrlen_      { sizeof(addr_) };
    std::string        desc_;
}; // class Kcp;


}; // namespace typhon::core;


#endif // __TYPHON_KCP_SESSION_HPP__