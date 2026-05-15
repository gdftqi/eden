#ifndef __TYPHON_KCP_EX_HPP__
#define __TYPHON_KCP_EX_HPP__


#include <memory>
#include <string.h>

#include "kcp/ikcp.h"
#include "typhon.in.hpp"
#include "package.hpp"


namespace typhon {


class KcpServer;


/**
 * @brief Kcp 类
 */
class Kcp: public std::enable_shared_from_this<Kcp> {
    Kcp(const Kcp&) = delete;
    Kcp& operator=(const Kcp&) = delete;
    Kcp(Kcp&&) = delete;
    Kcp& operator=(Kcp&&) = delete;


public:
    typedef std::shared_ptr<Kcp> Ptr;


    /**
     * @brief 创建一个 Kcp 实例(shared_ptr,便于跨 epoll cycle / sque_ 持有)。
     * @param conv   KCP conv ID,客户端 / 服务端必须一致
     * @param server 所属 KcpServer
     */
    static Ptr
    create(uint32_t conv, KcpServer* server, const void* addr, socklen_t addrlen) noexcept {
        return Ptr(new Kcp(conv, server, addr, addrlen));
    }


    /**
     * @brief KCP 行为参数(对应 ikcp_nodelay / ikcp_wndsize + session timeout)。
     *        进程级单例,**在创建任何 Kcp 实例前修改才生效**;已存在的 Kcp 不受影响,
     *        因为 KCP 参数在 ctor 里就被 snapshot 进 ikcpcb。
     */
    struct Conf {
        int      sndwnd   { 128 };   ///< 发送窗口
        int      rcvwnd   { 128 };   ///< 接收窗口
        int      nodelay  { 1 };     ///< 是否开启低延迟模式
        int      interval { 10 };    ///< update 间隔
        int      resend   { 3 };     ///< 快速重传, 表示连接跳过3个包的时候就会重传
        int      nc       { 1 };     ///< 是否关闭拥塞控制, 1为关闭, 0为不关闭
        uint32_t timeout  { 30000 }; ///< 超时(ms)
    };


    /**
     * @brief 进程级共享的 Kcp 配置单例。
     */
    static Conf& conf() noexcept {
        static Conf conf;
        return conf;
    }


    /**
     * @brief 获取 conv
     */
    static uint32_t
    getconv(const void* data, int len) noexcept {
        return len < 4 ? 0 : ::ikcp_getconv(data);
    }


    /**
     * @brief 析构函数
     */
    ~Kcp() noexcept {
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
    

    /**
     * @brief 所属服务
     */
    KcpServer*
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
    addrlen() noexcept {
        return addrlen_;
    }


    /**
     * @brief 对端地址(字符串)
     */
    std::string
    remote_addr() noexcept {
        return sockaddr_to_string((sockaddr*)&addr_);
    }


    /**
     * @brief 下一个发送幂等
     */
    uint32_t
    next_snd_idem() noexcept {
        return ++snd_idem_;
    }


    /**
     * @brief 检测超时
     */
    bool
    check_timeout(uint32_t tnow) const noexcept {
        return tnow - last_recv_ms_ > conf().timeout;
    }


    /**
     * @brief 推动 KCP 内部状态机:超时重传、发 ACK、flush 待发数据。
     *        必须按 ikcp_nodelay() 设的 interval 周期调 —— 不调用 KCP 不会推进,
     *        重传和 ACK 都会停。
     * @param current 单调递增的毫秒时间戳(KCP 只用 delta,起点无所谓,但所有 KCP
     *                调用必须用同一时间源)。典型用 CLOCK_MONOTONIC 转 ms。
     */
    void
    update(uint32_t current) noexcept {
        ::ikcp_update(kcp_, current);
    }


    /**
     * @brief 把对端发来的一段 UDP payload 喂给 KCP 状态机:解 KCP 头、放进 rcv_queue、
     *        触发 ACK / 重传。喂完之后才能用 recv_pk() 取出完整应用层消息。
     *        成功时同步记录对端地址,后续 output 回调通过 addr() 取到。
     * @param data    UDP payload 起始
     * @param len     payload 字节数
     * @param addr    对端 sockaddr(从 recvmmsg 拿到的 msg_hdr.msg_name)
     * @param addrlen addr 长度
     * @return  0  成功
     * @return <0  KCP 协议错(conv 不匹配 / segment 头损坏等);通常该包直接丢弃
     */
    int
    input(const void* data, long len, const void* addr, socklen_t addrlen) noexcept {
        int res = ::ikcp_input(kcp_, (const char*)data, len);
        if (res == 0) {
            ::memcpy(&addr_, addr, addrlen);
            addrlen_ = addrlen;
        }
        return res;
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
     * @param[out] pkg 解析成功时指向 buf 起始(host 字节序,可直接访问字段)
     * @param      buf 接收缓冲;长度应 >= PACK_MAX_LEN,否则会触发 ikcp_recv 的 -3
     * @param      len buf 长度
     * @param      now 当前 tnow_(用于刷新 last_recv_ms_)
     * @return   1  成功,*pkg 可用
     * @return   0  当前没消息(rcv_queue 空 / KCP 返回 <=0)
     * @return  -1  非法包:长度不足头 / pk_len 不自洽 / idem 违反单调性(重复包)
     */
    int
    recv_pk(Package** pkg, uint8_t* buf, int len, uint32_t now) noexcept {
        int res = recv(buf, len);
        if (res <= 0) {
            return 0;
        }

        if ((size_t)res < sizeof(Package)) {
            return -1;
        }

        *pkg = (Package*)buf;
        pk_ntoh(*pkg);
        if ((*pkg)->pk_len != res) {
            return -1;
        }

        if (rcv_idem_ >= (*pkg)->pk_idem) {
            return -1;
        }

        last_recv_ms_ = now;
        rcv_idem_ = (*pkg)->pk_idem;
        return 1;
    }


    /**
     * @brief 组装一条 Package(头 + payload)并入队 KCP 发送队列。
     *        内部用 thread_local 缓冲拼包再调 send(),不分配堆。
     *        客户端 / 服务端 stamp idem 的策略不同,**调用方负责传对 idem 值**:
     *          - 客户端: 自己维护 snd_idem_,单调递增
     *          - 服务端: 用 next_snd_idem()(本地计数器)
     * @param pk_id     业务消息号
     * @param pk_idemp  幂等 ID,**必须 != 0**;0 会被对端 recv_pk 视为非法包丢弃
     * @param pk_dst_id 目标服务类型(路由键);客户端发出时填,echo 时透传
     * @param data      payload 起始
     * @param len       payload 字节数;Package 总长(HEADER + payload)不可超过 UINT16_MAX
     * @return   0  入队成功(等 update() 触发出网卡)
     * @return  -1  Package 总长溢出 uint16(payload 太大)
     * @return  -2  KCP 拒绝入队(分片数 >= rcvwnd),见 send() 注释
     */
    int
    send_pk(uint16_t pk_id, uint32_t pk_idemp, uint32_t pk_dst_id, const uint8_t* data, uint16_t len) noexcept {
        thread_local static uint8_t buf[PACK_MAX_LEN];

        size_t total = sizeof(Package) + len;
        if (total > UINT16_MAX) {
            return -1;
        }

        auto* pkg = (Package*)buf;
        pkg->pk_len = total;
        pkg->pk_id = pk_id;
        pkg->pk_idem = pk_idemp;
        pkg->pk_dst_id = pk_dst_id;
        ::memcpy(pkg->pk_data, data, len);

        pk_hton(pkg);
        return send(buf, total);
    }


private:
    /**
     * @brief 构造函数
     */
    explicit
    Kcp(uint32_t conv, KcpServer* server, const void* addr, socklen_t addrlen) noexcept;


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


    KcpServer*          server_         { nullptr };
    uint32_t            last_recv_ms_   { 0 };
    uint32_t            snd_idem_       { 0 };
    uint32_t            rcv_idem_       { 0 };
    ::ikcpcb*           kcp_            { nullptr };
    ::sockaddr_storage  addr_           {};
    ::socklen_t         addrlen_        { sizeof(addr_) };
}; // class Kcp;


}; // namespace typhon;


#endif // __TYPHON_KCP_EX_HPP__