#include "kcp/session.hpp"
#include "kcp/server.hpp"
#include <format>


static constexpr uint8_t DIR_C2S = 0;   // client → server (上行, recv 解密用)
static constexpr uint8_t DIR_S2C = 1;   // server → client (下行, send 加密用)


// data_decode / data_encode 已移到 core/package.cpp(共享 codec, Session 用 data 段那对)


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
    Worker*     server,
    const void* addr,
    socklen_t   addrlen
) noexcept
    : server_(server)
    , json_(std::format("{{\"conv\":{},\"remote\":\"{}\"}}", conv, core::sockaddr_to_string((sockaddr*)addr))) {
    kcp_ = ::ikcp_create(conv, this);

    ::memcpy(&addr_, addr, addrlen);
    addrlen_ = addrlen;

    auto* c = Conf::instance();
    ::ikcp_wndsize(kcp_, c->sndwnd(), c->rcvwnd());
    ::ikcp_nodelay(kcp_, c->nodelay(), c->interval(), c->resend(), c->nc());
    ::ikcp_setmtu(kcp_, core::KCP_MTU);
    ::ikcp_set_siphash(kcp_, Conf::instance()->siphash());
    kcp_->timeout = 5000;
    set_output(Worker::output);
}


int
typhon::kcp::Session::recv(typhon::core::Package** pk) noexcept {
    thread_local static uint8_t rbuf[core::PKG_MAX_LEN];

    int res = ::ikcp_recv(kcp_, (char*)rbuf, sizeof(rbuf));
    if (res < 0) {
        return core::from_ikcp_recv(res);
    }

    if (res < core::PKG_DATA_LEN || res > core::PKG_MAX_LEN) {
        return xERR_PK_LEN;
    }

    bool encrypted = authed() && res > core::PKG_DATA_LEN;
    if (encrypted && res < core::PKG_DATA_LEN + (int)utils::XX20_TAG_LEN) {
        return xERR_PK_LEN;
    }

    size_t datalen = res;

    if (encrypted) {
        uint32_t seq;
        ::memcpy(&seq, rbuf + core::PKG_DATA_LEN - sizeof(uint32_t), sizeof(seq));
        seq = core::u32_to_le(seq);

        size_t   clen   = (size_t)res - core::PKG_DATA_LEN - utils::XX20_TAG_LEN;
        uint8_t* cipher = rbuf + core::PKG_DATA_LEN;
        uint8_t* tag    = cipher + clen;

        uint8_t nonce[utils::XX20_NONCE_LEN];
        make_nonce(nonce, conv(), seq, DIR_C2S);
        if (utils::xx20_decrypt(cipher, clen, cipher, tag, rx_key_, nonce)) {
            return xERR_PK_DEC;
        }

        datalen = core::PKG_DATA_LEN + clen;
    }

    // 客户端侧 data-only, meta 是本地合成: conv=会话 conv, src_addr=客户端地址
    core::Package* p = core::data_decode(rbuf, datalen);
    if (p == nullptr) {
        return xERR_PK_LEN;
    }
    p->meta.conv     = conv();
    p->meta.src_addr = remote_addr_u32();

    int err = xOK;

    if (p->data.id == 0) {
        err = xERR_PK_ID;
    } else if (p->data.src_id == 0) {
        err = xERR_PK_SRC; 
    } else if (p->data.dst_id == 0) {
        err = xERR_PK_DST;
    } else if (p->data.seq == 0) {
        err = xERR_PK_SEQ;
    } else if (rcv_req_ >= p->data.seq) {
        err = xDUP;
    }

    if (err != xOK) {
        ::mi_free(p);
        return err;
    }

    rcv_req_ = p->data.seq;
    *pk = p;
    return xOK;
}


int
typhon::kcp::Session::send(core::Package *pk) noexcept  {
    // 发送时最大的 payload 长度
    constexpr int SND_MAX_PAYLOAD = core::PKG_MAX_LEN - core::PKG_HDR_LEN - utils::XX20_TAG_LEN;

    // 发送缓冲区
    thread_local static uint8_t sndbuf[core::PKG_MAX_LEN];

    int plen = (int)pk->payload_length();
    if (plen > SND_MAX_PAYLOAD) {
        return xERR_PK_LEN;
    }

    pk->data.seq = next_snd_seq();

    int wire;
    if (plen > 0 && authed()) {
        core::data_encode(sndbuf, pk);

        uint8_t nonce[utils::XX20_NONCE_LEN];
        make_nonce(nonce, conv(), pk->data.seq, DIR_S2C);
        ASSERT(utils::xx20_encrypt(sndbuf + core::PKG_DATA_LEN, plen,
                                   sndbuf + core::PKG_DATA_LEN,
                                   sndbuf + core::PKG_DATA_LEN + plen,
                                   tx_key_, nonce) == 0, "加密失败");

        wire = core::PKG_DATA_LEN + plen + (int)utils::XX20_TAG_LEN;
    } else {
        wire = core::data_encode(sndbuf, pk);
    }

    return core::from_ikcp_send(::ikcp_send(kcp_, (char*)sndbuf, wire));
}