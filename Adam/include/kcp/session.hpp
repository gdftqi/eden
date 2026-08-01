#ifndef __ADAM_KCP_SESSION_HPP__
#define __ADAM_KCP_SESSION_HPP__


#include <absl/container/flat_hash_set.h>
#include "core/adam.in.hpp"
#include "core/package.hpp"
#include "core/error.hpp"
#include "kcp/config.hpp"
#include "kcp/ikcp.h"


namespace adam::kcp {


class Worker;


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
    typedef uint8_t Xx20Key[utils::XX20_KEY_LEN];
    typedef absl::flat_hash_set<uint32_t> BindSet;


    /**
     * @brief 创建一个 Kcp 实例(shared_ptr,便于跨 epoll cycle / sque_ 持有).
     * @param conv   KCP conv ID,客户端 / 服务端必须一致
     * @param server 所属 KcpServer
     */
    static Ptr
    create(uint32_t conv, Worker* worker, const void* addr, socklen_t addrlen) noexcept {
        return std::make_shared<Session>(conv, worker, addr, addrlen);
    }


    /**
     * @brief 从信封里取 conv
     */
    static uint32_t
    getconv(const uint8_t* data, size_t len) noexcept {
        if (len < core::ENVELOPE_HDR_LEN) {
            return 0;
        }

        const uint32_t* p = (const uint32_t*)(data + core::ENVELOPE_CONV_OFF);
        return core::u32_to_le(*p);
    }


    /**
     * @brief 构造函数
     */
    explicit
    Session(uint32_t conv, Worker* worker, const void* addr, socklen_t addrlen) noexcept;


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
    to_json() const noexcept {
        return json_;
    }


    bool
    authed() const noexcept {
        return uid_ > 0;
    }


    uint32_t
    uid() const noexcept {
        return uid_;
    }


    void
    set_uid(uint32_t uid) noexcept {
        uid_ = uid;
        kcp_->timeout = Conf::instance()->server()->timeout;
        json_ = std::format("{{\"user_id\":{},\"conv\":{},\"remote\":\"{}\"}}", uid, kcp_->conv, core::sockaddr_to_string((sockaddr*)&addr_));
    }
    

    /**
     * @brief 所属服务
     */
    Worker*
    worker() noexcept {
        return worker_;
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


    void
    set_key(const uint8_t* tx, const uint8_t* rx) noexcept {
        ::memcpy(tx_key_, tx, utils::XX20_KEY_LEN);
        ::memcpy(rx_key_, rx, utils::XX20_KEY_LEN);
        ::ikcp_open(kcp_);
    }


    const BindSet&
    binds() const noexcept {
        return binds_;
    }


    /**
     * @brief 连续解密失败次数(收到任何一条可解密的包即清零)
     */
    uint32_t
    decode_fail() const noexcept {
        return dec_fail_;
    }


    /**
     * @brief 主动踢除本终端
     */
    int
    kick(uint32_t code) noexcept {
        return ::ikcp_send_kick(kcp_, code);
    }


    /**
     * @brief 被踢时对端给的原因码(仅客户端侧有意义)
     */
    uint32_t
    kick_code() const noexcept {
        return kcp_->kick_code;
    }


    /**
     * @brief 终端上线: 向路由服务登记
     *
     * @warning 同一个 uid 必需路由到同一个 router 服务中, 否则 顶号将无效
     */
    void
    terminal_enter() noexcept;


    /**
     * @brief 终端下线: 向绑定集里的每个后端发 OFF, 让它们清掉档案
     *
     * @param code 离开码
     */
    void
    terminal_off(uint32_t code) noexcept;


    /**
     * @brief 回一条 PID_ERROR 告知客户端"这条请求没送到"
     *
     * @param code   PERR_REQ_*
     * @param dst_id 原请求的目标服务 id
     * @param pid    原请求的 PID
     *
     * @note 与 kick 的区别 -- 这里连接不结束, 客户端可以重试或换目标.
     *       只对已鉴权会话有意义: 未鉴权的对端还没有证明自己是谁, 不值得回包.
     */
    void
    send_error(uint32_t code, uint32_t dst_id, uint32_t pid) noexcept;


    /**
     * @brief 绑定集
     */
    void
    bind(uint32_t sid) noexcept {
        binds_.insert(sid);
    }


    void
    unbind(uint32_t sid) noexcept {
        binds_.erase(sid);
    }


    /**
     * @brief 推动 KCP 内部状态机: 超时重传 / 发 ACK / flush 待发数据.
     * 必须按 ikcp_nodelay() 设的 interval 周期调 -- 不调用 KCP 不会推进.
     *
     * @return   0                     正常(本轮可能 flush 了数据, 也可能什么都没做)
     *         -50 IKCP_ERR_TIMEOUT    超过 kcp_->timeout 未收到对端任何数据 -> 判死
     *         -51 IKCP_ERR_RST        收到对端 RST, 会话在对端已不存在 -> 判死
     *         -52 IKCP_ERR_KICKED     收到对端 KICK, 被服务端踢除 -> 判死
     *
     * 返回值直接是 ikcp 层的码(未经框架层翻译), 打日志用 ikcp_error() 取字符串.
     *
     * @note 返回负值即会话已死, 调用方应摘除会话(worker::update 里按 < 0 处理)
     *       首次调用会初始化保活时刻, 不会误判超时
     */
    int
    update(uint64_t current) noexcept {
        return ::ikcp_update(kcp_, (uint32_t)current);
    }


    /**
     * @brief 把对端发来的一段 UDP payload 喂给 KCP 状态机:解 KCP 头 / 放进 rcv_queue /
     * 触发 ACK / 重传. 喂完之后才能用 recv_pk() 取出完整应用层消息.
     * 成功时同步记录对端地址,后续 output 回调通过 addr() 取到.
     * @param data    UDP payload 起始
     * @param len     payload 字节数
     * @param addr    对端 sockaddr(从 recvmmsg 拿到的 msg_hdr.msg_name)
     * @param addrlen addr 长度
     * @return  xOK          成功
     *
     *          xERR_PK_DEC  信封解不开(伪造/重放)
     *
     *          IKCP_ERR_*   ikcp_input 的码原样上抛, 见 kcp/ikcp.h 的 -2x 段;
     *                       另外包长不足一个信封头时直接返回 IKCP_ERR_TOOSHORT
     *
     * @note 拿到任何 input 错误都只丢包, 不摘会话 -- 与 ikcp.c 里
     * "返回错误码而不是改状态" 那段注释是一对, 改一边前先看另一边.
     *
     * @note 地址重绑定(NAT 漫游)不需要额外判据: 翻转之后能走到 ikcp_input 的包
     * 都已通过 AEAD + 重放窗口, 本身就是"可证新鲜且来自对端"的凭据.
     */
    int
    input(const uint8_t* data, size_t len, const void* addr, socklen_t addrlen) noexcept;


    /**
     * @brief 加密
     */
    int
    sealedbox_encode(const uint8_t* dg, size_t len, uint8_t* out) noexcept;


    /**
     * @brief 解封(seal 的逆): 查重放窗口 -> AEAD 解密 -> 提交窗口. 顺序不能反,
     * 窗口必须等验签通过才提交(否则伪造包能把窗口推远, 顶掉对端的真包).
     *
     * @param wire 线上字节
     * @param len wire 长度
     * @param out 输出缓冲, 至少 len 字节(明文一定比密文短)
     *
     * @return 裸 KCP 数据报长度; <0 表示这不是一个能解开的信封
     * (调用方据此决定: 过渡期当明文处理, 已翻转则丢弃)
     */
    int
    sealedbox_decode(const uint8_t* wire, size_t len, uint8_t* out) noexcept;


    void
    set_output(int (*output)(const uint8_t* buf, int len, struct IKCPCB *kcp)) noexcept {
        ::ikcp_setoutput(kcp_, output);
    }


    /**
     * @brief 从 KCP 队列取出一条完整消息, 解码进调用方的 pk 并做字段自检.
     *
     * 中转缓冲是本方法内部的 thread_local, 调用方不用管. meta.conv / meta.src_addr
     * 不在线上传输, 由本方法按会话自身状态补齐.
     *
     * @param[out] pk 解码目标(host 序); 需能容纳 PKG_MAX_LEN
     *
     * @return  xOK    成功(长度在 pk->meta.len 里)
     *
     *          xAGAIN rcv_queue 空 / 分片未集齐, 当前没有更多消息
     *
     *          IKCP_ERR_BUFSMALL  单条消息超过 PKG_MAX_LEN, 装不下
     *
     *          xERR_PK_LEN/PID/SRC/DST  字段自检失败(见 core/error.hpp)
     *
     * @note 不做重复包判定 -- KCP 的 ikcp_parse_data 已按 sn 去重, 重复段只回 ACK
     * 不入 rcv_queue, 走不到这里.
     */
    int
    recv(core::Package* pk) noexcept;


    /**
     * @brief 把一条应用层 Package 交给 KCP 发出: 序列化 Package::data(小端)到内部
     *        thread_local 缓冲, 再经 ikcp_send 入发送队列(实际上线由 update() 的 flush 完成).
     *
     * @param pk 待发送包(host 序), 只读, 不会被原地修改
     *
     * @return  xOK               成功入 KCP 发送队列(可靠传输, 但不代表对端已收到)
     *
     *          xERR_PK_LEN       payload 超限(wire 会超 PKG_MAX_LEN, 对端收不下)
     *
     *          IKCP_ERR_*        ikcp_send 的码原样上抛(PARAM / TOOBIG / NOMEM)
     *
     * @note 这一层不加密. 加解密在信封层(sealedbox_encode), 由 Worker::output
     * 在数据报真正出网卡时整体套上 -- 包住的是整个 KCP 数据报, 不是单条消息.
     */
    int
    send(core::Package *pk) noexcept;


private:
    /**
     * @brief 判定"这段字节不是对端发的": 累计一次并返回 xERR_PK_DEC.
     *        调用方(Worker)看到该码只丢包, 连续超过阈值才摘会话.
     */
    int
    reject() noexcept {
        ++dec_fail_;
        return xERR_PK_DEC;
    }


    /**
     * @brief 查防重放窗口
     *
     * @return true  没见过, 可以收(但要等 AEAD 验过才能 replay_commit)
     *         false 拒收, 两种情况:
     *               1. 落在窗口内且对应 bit 已置位 = 重放
     *               2. 比 rcv_ctr_max_ - 63 还老 = 太旧, 窗口装不下, 一律当重放处理
     */
    bool
    replay_ok(uint32_t ctr) const noexcept {
        if (ctr > rcv_ctr_max_) {
            return true;
        }

        uint32_t back = rcv_ctr_max_ - ctr;
        return back < 64 && (rcv_ctr_bits_ & (1ULL << back)) == 0;
    }


    /**
     * @brief 把 ctr 记进窗口
     */
    void
    replay_commit(uint32_t ctr) noexcept {
        if (ctr > rcv_ctr_max_) {
            uint32_t shift = ctr - rcv_ctr_max_;
            rcv_ctr_bits_  = (shift < 64) ? ((rcv_ctr_bits_ << shift) | 1ULL) : 1ULL;
            rcv_ctr_max_   = ctr;
            return;
        }

        rcv_ctr_bits_ |= (1ULL << (rcv_ctr_max_ - ctr));
    }


    Worker*            worker_   { nullptr };
    uint32_t           uid_      { 0 };
    uint32_t dec_fail_ { 0 };
    ::ikcpcb*          kcp_      { nullptr };
    ::sockaddr_storage addr_     {};
    ::socklen_t        addrlen_  { sizeof(addr_) };
    Xx20Key            tx_key_   { 0 };
    Xx20Key            rx_key_   { 0 };
    BindSet binds_;
    std::string        json_;

    // ---------------- 信封层(见 adam.in.hpp 的布局说明) ----------------

    Conf::SipHashKey slot_key_     { 0 };
    uint32_t         snd_ctr_      { 0 };
    uint32_t         rcv_ctr_max_  { 0 };
    uint64_t         rcv_ctr_bits_ { 1 };
    bool             env_up_       { false };
}; // class Kcp;


}; // namespace adam::core;


#endif // __ADAM_KCP_SESSION_HPP__
