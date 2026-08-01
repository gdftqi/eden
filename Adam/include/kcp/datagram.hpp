#ifndef __ADAM_KCP_DATAGRAM_HPP__
#define __ADAM_KCP_DATAGRAM_HPP__


#include <deque>
#include "core/adam.in.hpp"


namespace adam::kcp {


/** 
 * @brief UDP 数据报
 * 
 * 用于 kcp 中 output 中发送消息
 */
struct Datagram {
    Datagram(const Datagram&) = delete;
    Datagram& operator=(const Datagram&) = delete;
    Datagram(Datagram&&) = delete;
    Datagram& operator=(Datagram&&) = delete;


    typedef std::deque<Datagram*> Que; // SndBuf 队列


    ::sockaddr_storage addr;               // 对端地址
    ::socklen_t        addrlen;            // 地址长度
    uint32_t           len;                // wire 数据报长度(已含信封)
    uint64_t           time;               // 缓冲区创建的时间

    // 已封装好的完整 wire 数据报, 由 Worker::output 调 Session::sealedbox_encode 产出.
    // 两种形态: 握手期 [8B 槽位MAC][裸 KCP 数据报],
    //           翻转后   [8B 槽位MAC][conv 4B][计数器 4B][AEAD 密文][tag 16B]
    uint8_t            buf[core::UDP_MTU];


    explicit
    Datagram(const void* addr, ::socklen_t addrlen, const uint8_t* b, uint32_t l, uint64_t time) noexcept
        : addrlen(addrlen)
        , len(l)
        , time(time) {
        ::memcpy(&this->addr, addr, addrlen);
        ASSERT(l <= core::UDP_MTU, "len = {}, max = {}", l, core::UDP_MTU);
        ::memcpy(this->buf, b, l);
    }
}; // struct Datagram;

    
} // namespace adam::kcp


#endif // __ADAM_KCP_DATAGRAM_HPP__
