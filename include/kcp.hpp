#ifndef __TYPHON_KCP_EX_HPP__
#define __TYPHON_KCP_EX_HPP__


#include <memory>
#include <string.h>

#include "kcp/ikcp.h"
#include "typhon.in.hpp"


namespace typhon {


class KcpServer;


class Kcp: public std::enable_shared_from_this<Kcp> {
    Kcp(const Kcp&) = delete;
    Kcp& operator=(const Kcp&) = delete;
    Kcp(Kcp&&) = delete;
    Kcp& operator=(Kcp&&) = delete;


public:
    typedef std::shared_ptr<Kcp> Ptr;


    static Ptr
    create(uint32_t conv, const void* addr, socklen_t addrlen, KcpServer* server) noexcept {
        return std::make_shared<Kcp>(conv, (const ::sockaddr_storage*)addr, addrlen, server);
    }

    
    struct Conf {
        int sndwnd { 128 };
        int rcvwnd { 128 };
        int nodelay { 1 };
        int interval { 10 };
        int resend { 3 };
        int nc { 1 };
        uint32_t timeout { 60000 };
    };


    static Conf& conf() noexcept {
        static Conf conf;
        return conf;
    }


    static uint32_t
    getconv(const void* data, int len) noexcept {
        return len < 4 ? 0 : ::ikcp_getconv(data);
    }


    explicit Kcp(uint32_t conv, const ::sockaddr_storage* addr, socklen_t addrlen, KcpServer* server) noexcept
        : server_(server) {
        kcp_ = ::ikcp_create(conv, this);

        auto& c = conf();
        ::ikcp_wndsize(kcp_, c.sndwnd, c.rcvwnd);
        ::ikcp_nodelay(kcp_, c.nodelay, c.interval, c.resend, c.nc);
        ::ikcp_setmtu(kcp_, UDP_MTU);

        ::memcpy(&addr_, addr, addrlen);
        addrlen_ = addrlen;
    }


    ~Kcp() noexcept {
        if (kcp_) {
            ::ikcp_release(kcp_);
        }
    }


    uint32_t
    conv() const noexcept {
        return kcp_->conv;
    }


    bool
    timeout(uint32_t tnow) const noexcept {
        return tnow - last_recv_ms_ > conf().timeout;
    }
    

    int
    recv(uint8_t* buf, int len) noexcept {
        return ::ikcp_recv(kcp_, (char*)buf, len);
    }


    int
    send(const uint8_t* buf, int len) noexcept {
        return ::ikcp_send(kcp_, (const char*)buf, len);
    }


    // 推动 KCP 内部状态机：超时重传、发 ACK、flush 待发数据。
    // 必须按 ikcp_nodelay() 设的 interval 周期调——不调用 KCP 不会推进，
    // 重传和 ACK 都会停。
    // current: 单调递增的毫秒时间戳（KCP 只用 delta，起点无所谓，但
    //          所有 KCP 调用必须用同一时间源）。典型用 CLOCK_MONOTONIC 转 ms。
    void
    update(uint32_t current) noexcept {
        ::ikcp_update(kcp_, current);
    }


    uint32_t
    check(uint32_t current) const noexcept {
        return ::ikcp_check(kcp_, current);
    }


    int
    input(const void* data, long len, uint32_t now) noexcept {
        int res = ::ikcp_input(kcp_, (const char*)data, len);
        if (res == 0) {
            last_recv_ms_ = now;
        }
        return res;
    }


    // 立即把待发数据 / ACK / 超时重传通过 output 回调发出去。
    // ikcp_send 只是入队，真正出网卡靠 update 周期性触发 flush，或手动调 flush 提前触发。
    // 何时手动调：发完一条延迟敏感的消息后立刻调，省掉 ≤ interval 的等待。
    // 何时不该调：高吞吐连续 send 时频繁调会破坏 MTU 批量打包，效率反而降低。
    void
    flush() noexcept {
        ::ikcp_flush(kcp_);
    }


    int
    peeksize() const noexcept {
        return ::ikcp_peeksize(kcp_);
    }


    void
    set_output(int (*output)(const char *buf, int len, struct IKCPCB *kcp, void *user)) noexcept {
        ::ikcp_setoutput(kcp_, output);
    }


    KcpServer*
    server() noexcept {
        return server_;
    }


    ::sockaddr_storage*
    addr() noexcept {
        return &addr_;
    }


    ::socklen_t
    addrlen() noexcept {
        return addrlen_;
    }


    std::string
    remote_addr() noexcept {
        return sockaddr_to_string((sockaddr*)&addr_);
    }


private:
    KcpServer* server_ { nullptr };
    uint32_t last_recv_ms_ { 0 };
    ::ikcpcb* kcp_ { nullptr };
    ::sockaddr_storage addr_ {};
    ::socklen_t addrlen_ { sizeof(addr_) };
}; // class Kcp;


}; // namespace typhon;


#endif // __TYPHON_KCP_EX_HPP__