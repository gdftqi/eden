#ifndef __TYPHON_KCP_EX_HPP__
#define __TYPHON_KCP_EX_HPP__


#include "kcp/ikcp.h"
#include "typhon.in.h"



namespace typhon {


class Kcp {
    Kcp(const Kcp&) = delete;
    Kcp& operator=(const Kcp&) = delete;
    Kcp(Kcp&&) = delete;
    Kcp& operator=(Kcp&&) = delete;


public:
    struct Conf {
        int sndwnd { 256 };
        int rcvwnd { 256 };
        int nodelay { 1 };
        int interval { 10 };
        int resend { 3 };
        int nc { 1 };
    };


    static Conf& conf() noexcept {
        static Conf conf;
        return conf;
    }


    static void
    allocator(void* (*new_malloc)(size_t), void (*new_free)(void*)) {
        ::ikcp_allocator(new_malloc, new_free);
    }


    explicit Kcp(uint32_t conv, void* user) noexcept {
        kcp_ = ::ikcp_create(conv, user);

        auto& c = conf();
        ::ikcp_wndsize(kcp_, c.sndwnd, c.rcvwnd);
        ::ikcp_nodelay(kcp_, c.nodelay, c.interval, c.resend, c.nc);
        ::ikcp_setmtu(kcp_, UDP_MTU);
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
    input(const uint8_t* data, long len) noexcept {
        return ::ikcp_input(kcp_, (const char*)data, len);
    }


    void
    flush() noexcept {
        ::ikcp_flush(kcp_);
    }


    int
    peeksize() const noexcept {
        return ::ikcp_peeksize(kcp_);
    }


private:
    ::ikcpcb* kcp_ { nullptr };
}; // class Kcp;


}; // namespace typhon;


#endif // __TYPHON_KCP_EX_HPP__