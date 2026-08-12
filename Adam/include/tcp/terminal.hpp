#ifndef __ADAM_TCP_TERMINAL_HPP__
#define __ADAM_TCP_TERMINAL_HPP__


#include "tcp/session.hpp"


namespace adam::tcp {


class Terminal {
    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;
    Terminal(Terminal&&) = delete;
    Terminal& operator=(Terminal&&) = delete;


public:
    enum class Type {
        PC,     // 电脑
        Mobile, // 手机
        Pad,    // 平板
    };


    typedef std::shared_ptr<Terminal>          Ptr;
    typedef absl::flat_hash_map<uint32_t, Ptr> Map;


    static Ptr
    create(uint32_t uid, uint32_t conv, uint32_t ip, uint16_t port, Session::Ptr s) noexcept {
        return Ptr(new Terminal(uid, conv, ip, port, std::move(s)));
    }


    uint32_t
    uid() const noexcept {
        return uid_;
    }


    uint32_t
    conv() const noexcept {
        return conv_;
    }


    const Session::Ptr&
    sess() const noexcept {
        return sess_;
    }


    /**
     * @brief 发包(唯一组帧点). 只允许在本终端的属主 reactor 线程上调用(Session::send 无锁).
     * @param dst 0 = 发给终端本人(业务包, 网关按 conv 转发);
     *            传网关 id = 发给网关自己的框架通知(KIC/BIND/UNBD)
     * @return >= 0 成功, < 0 socket 错
     */
    int
    send(uint16_t pid, const uint8_t* payload, uint32_t len, uint32_t dst = 0) noexcept;


    /**
     * @brief 踢除本终端(位置透明): 属主 reactor == cur 直接踢, 否则投 TerminalKick 给属主
     */
    void
    kick(uint32_t code, Reactor* cur) noexcept;


    /**
     * @brief 告知网关: 本服务已接管该终端, 其下线时请通知我(加入网关的绑定集)
     */
    void
    bind() noexcept;


    /**
     * @brief 告知网关: 本服务不再关心该终端(移出网关的绑定集)
     */
    void
    unbind() noexcept;


private:


    explicit
    Terminal(uint32_t uid, uint32_t conv, uint32_t ip, uint16_t port, Session::Ptr s) noexcept
        : uid_(uid)
        , conv_(conv)
        , sess_(std::move(s)) {
        core::u32_to_sockaddr((sockaddr_in*)&addr_, ip, port);
    }


    uint32_t           uid_  { 0 };
    uint32_t           conv_ { 0 };
    ::sockaddr_storage addr_ {};
    Session::Ptr       sess_;
}; // class Terminal;


} // namespace adam::tcp


#endif // __ADAM_TCP_TERMINAL_HPP__
