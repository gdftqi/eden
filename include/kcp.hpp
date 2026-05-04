#ifndef __TYPHON_KCP_EX_HPP__
#define __TYPHON_KCP_EX_HPP__


#include <memory>
#include <string.h>

#include "kcp/ikcp.h"
#include "typhon.in.hpp"


namespace typhon {


class UdpServer;


class Kcp {
    Kcp(const Kcp&) = delete;
    Kcp& operator=(const Kcp&) = delete;
    Kcp(Kcp&&) = delete;
    Kcp& operator=(Kcp&&) = delete;


public:
    typedef std::shared_ptr<Kcp> Ptr;


    static Ptr
    create(uint32_t conv, const void* addr, socklen_t addrlen) noexcept {
        return Ptr(new Kcp(conv, (::sockaddr_storage*)addr, addrlen));
    }

    
    struct Conf {
        int sndwnd { 32 };
        int rcvwnd { 32 };
        int nodelay { 1 };
        int interval { 10 };
        int resend { 3 };
        int nc { 1 };
    };


    static Conf& conf() noexcept {
        static Conf conf;
        return conf;
    }


    static uint32_t
    getconv(const void* data, int len) noexcept {
        return len < 4 ? 0 : ::ikcp_getconv(data);
    }


    static void
    allocator(void* (*new_malloc)(size_t), void (*new_free)(void*)) {
        ::ikcp_allocator(new_malloc, new_free);
    }


    ~Kcp() noexcept {
        if (kcp_) {
            ::ikcp_release(kcp_);
        }
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
    input(const void* data, long len) noexcept {
        return ::ikcp_input(kcp_, (const char*)data, len);
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
        kcp_->output = output;
    }


    void
    set_server(UdpServer* server) noexcept {
        server_ = server;
    }


    UdpServer*
    get_server() noexcept {
        return server_;
    }


    ::sockaddr_storage*
    addr() noexcept {
        return &addr_;
    }


    ::socklen_t*
    addrlen() noexcept {
        return &addrlen_;
    }


private:
    explicit Kcp(uint32_t conv, const ::sockaddr_storage* addr, socklen_t addrlen) noexcept {
        kcp_ = ::ikcp_create(conv, this);

        auto& c = conf();
        ::ikcp_wndsize(kcp_, c.sndwnd, c.rcvwnd);
        ::ikcp_nodelay(kcp_, c.nodelay, c.interval, c.resend, c.nc);
        ::ikcp_setmtu(kcp_, UDP_MTU);

        ::memcpy(&addr_, addr, addrlen);
        addrlen_ = addrlen;
    }


    UdpServer* server_ { nullptr };
    ::ikcpcb* kcp_ { nullptr };
    ::sockaddr_storage addr_ {};
    ::socklen_t addrlen_ { sizeof(addr_) };
}; // class Kcp;


}; // namespace typhon;


#endif // __TYPHON_KCP_EX_HPP__