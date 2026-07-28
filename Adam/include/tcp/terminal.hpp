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
    /**
     * @brief 终端离开本服务的原因(随 IHook::on_terminal_leave 回调给业务)
     */
    enum class Reason {
        Leave,        // 客户端主动自离(TER_LEA_REQ)
        Offline,      // 网关通知该终端已下线(TER_OFF_NTF): 超时/断线/被踢
        GatewayLost,  // 网关连接断开的批量清扫(粗粒度兜底)
    };


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
    // 只做组帧 + 发送; payload 由各自的 proto 结构编好后传入,
    // 于是 BIND/UNBD 的载荷可各自独立演进, 而信封约定只有一份。
    void
    notify(uint16_t pid, const uint8_t* payload, uint32_t len) noexcept;


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
