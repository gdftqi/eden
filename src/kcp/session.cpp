#include "kcp/session.hpp"
#include "kcp/server.hpp"
#include <format>


static constexpr uint8_t DIR_C2S = 0;   // client → server (上行, recv 解密用)
static constexpr uint8_t DIR_S2C = 1;   // server → client (下行, send 加密用)


static inline void
make_nonce(uint8_t nonce[typhon::utils::XX20_NONCE_LEN], uint32_t conv, uint32_t seq, uint8_t dir) noexcept {
    // 12B nonce = conv(4) | seq(4) | dir(1) | 0(3)
    // 同一会话密钥下 (conv, seq, dir) 唯一 → nonce 绝不复用 (seq 单调递增)
    ::memset(nonce, 0, typhon::utils::XX20_NONCE_LEN);
    ::memcpy(nonce + 0, &conv, sizeof(conv));
    ::memcpy(nonce + 4, &seq, sizeof(seq));
    nonce[8] = dir;
}


typhon::kcp::Session::Session(
    uint32_t    conv,
    Server*     server,
    const void* addr,
    socklen_t   addrlen
) noexcept
    : server_(server)
    , desc_(std::format("[{}]{}", conv, core::sockaddr_to_string((sockaddr*)addr))) {
    kcp_ = ::ikcp_create(conv, this);

    ::memcpy(&addr_, addr, addrlen);
    addrlen_ = addrlen;

    auto* c = Conf::instance();
    ::ikcp_wndsize(kcp_, c->sndwnd(), c->rcvwnd());
    ::ikcp_nodelay(kcp_, c->nodelay(), c->interval(), c->resend(), c->nc());
    ::ikcp_setmtu(kcp_, core::KCP_MTU);

    last_recv_ms_ = server->tnow();
}


int
typhon::kcp::Session::recv(core::PK<core::Host>* pk, uint8_t* buf, int len, uint64_t now) noexcept {
    int res = ::ikcp_recv(kcp_, (char*)buf, len);
    if (res < 0) {
        // ikcp_recv: -1/-2 无完整包 → xAGAIN, -3 buf 太小 → xERR_KCP_BUFSMALL
        return core::from_ikcp_recv(res);
    }

    if (res < core::PKG_HDR_LEN || res > core::PKG_MAX_LEN) {
        return xERR_PK_LEN;
    }

    // payload 可为 0: 空包(res == PKG_HDR_LEN)放行, 不解密;
    // 仅"authed 且有 payload"的包必须带 16B tag, 否则连 tag 都放不下 = 畸形。
    if (authed_ && res > core::PKG_HDR_LEN && res < core::PKG_HDR_LEN + (int)utils::XX20_TAG_LEN) {
        return xERR_PK_LEN;
    }

    *pk = core::ntoh(core::PK<core::Net>(buf, res));
    auto& p = *pk;

    if (p->id == 0) {
        return xERR_PK_ID;
    }

    if (p->seq == 0) {
        return xERR_PKT_SEQ;
    }

    if (p->dst_id == 0) {
        return xERR_PKT_DST;
    }

    if (rcv_req_ >= p->seq && p->id != PKID_REGIST_REQ) {
        // 幂等重复包, 跳过.
        // 但是, 如果是 PDID_REGIST_REQ 可以通过, 因为有可能是断线重连的
        return xDUP;
    }

    if (authed_ && res > core::PKG_HDR_LEN) {
        if (res < core::PKG_HDR_LEN + (int)utils::XX20_TAG_LEN) {
            return xERR_PK_LEN;
        }

        size_t   cipher_len = (size_t)res - core::PKG_HDR_LEN - utils::XX20_TAG_LEN;
        uint8_t* cipher     = buf + core::PKG_HDR_LEN;
        uint8_t* tag        = cipher + cipher_len;

        uint8_t nonce[utils::XX20_NONCE_LEN];
        make_nonce(nonce, conv(), p->seq, DIR_C2S);
        if (utils::xx20_decrypt(cipher, cipher_len, cipher, tag, rx_key_, nonce)) {
            return xERR_PK_DEC;
        }

        // 剥掉 tag: 上层 *pk 只到明文 payload 末尾, len_ 不含 16B tag
        *pk = core::PK<core::Host>((void*)buf, core::PKG_HDR_LEN + (int)cipher_len);
    }

    last_recv_ms_ = now;
    rcv_req_ = p->seq;
    return xOK;
}


int
typhon::kcp::Session::send(core::PK<core::Host> &pk) noexcept  {
    int plen = pk.len() - core::PKG_HDR_LEN;
    int len  = pk.len();
    pk->seq  = next_snd_seq();

    if (plen > 0 && authed_) {
        uint8_t nonce[utils::XX20_NONCE_LEN];
        make_nonce(nonce, conv(), pk->seq, DIR_S2C);
        uint8_t* tag = pk->payload + plen;
        ASSERT(utils::xx20_encrypt(pk->payload, plen, pk->payload, tag, tx_key_, nonce) == 0, "加密失败");
        len += (int)utils::XX20_TAG_LEN;
    }

    core::hton(pk);
    int res = core::from_ikcp_send(::ikcp_send(kcp_, (char*)pk.raw(), len));
    if (res >= xOK) {
        ::ikcp_flush(kcp_);
    }

    return res;
}