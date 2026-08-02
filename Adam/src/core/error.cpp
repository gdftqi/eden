#include "core/error.hpp"
#include "kcp/ikcp.h"


// str_error 要按号段转给 ikcp_error(), 所以本文件依赖 kcp 层.
// 依赖只落在这个 .cpp 里, core/error.hpp 仍然是纯宏 + 声明, 谁都能 include.
//
// 号段速查(四层互不重叠, 所以一个函数就够):
//     [-99, -12]     ikcp        转给 ikcp_error()
//     [-999, -200]   服务内部     x*
//     [1000, 9999]   服务之间     SERR_*
//     [10000, ...]   public      PERR_*
//     0              成功        xOK 与 SERR_OK 同值


const char*
adam::core::str_error(int ec) noexcept {
    // 0 同时是 xOK 和 SERR_OK
    if (ec == xOK) {
        return "ok";
    }

    // ------------------------- 负数: 进程内的码 -------------------------
    if (ec < 0) {
        // xAGAIN 取的是 -EAGAIN, 落在 ikcp 的两位数段里, 必须先按恒等拦下
        if (ec == xAGAIN) {
            return "try again";
        }

        // 两位数段 = ikcp 层
        if (ec > -100) {
            return ::ikcp_error(ec);
        }

        switch (ec) {
            case xERR:
                return "unspecified failure";

            case xERR_PARAM:
                return "invalid argument or output buffer too small";

            case xERR_NO_HANDLER:
                return "no handler registered for this pid";

            case xERR_REJECTED:
                return "rejected by business hook";

            case xERR_PK_LEN:
                return "invalid package length";

            case xERR_PK_PID:
                return "invalid pid";

            case xERR_PK_DST:
                return "invalid dst_id";

            case xERR_PK_SRC:
                return "invalid src_id";

            case xERR_PK_DEC:
                return "envelope decryption failed";

            case xERR_TOKEN_EXP:
                return "token expired";

            case xERR_TOKEN_VER:
                return "token signature verification failed";

            case xERR_X25519_KX:
                return "x25519 key exchange failed";

            case xERR_NOT_AUTH:
                return "not authenticated";

            case xERR_TOKEN_CONV:
                return "token conv mismatch";

            case xERR_TOKEN_USER:
                return "token user_id mismatch";

            case xERR_PK_STATE:
                return "pid not allowed in current session state";

            case xERR_SEALEDBOX:
                return "sealedbox seal/open failed";

            case xERR_TCP_CLOSED:
                return "peer closed the connection";

            case xERR_CONF_MISSING:
                return "required config field missing";

            case xERR_CONF_VALUE:
                return "invalid config field value";

            case xERR_BPF_OPEN:
                return "bpf_object__open failed";

            case xERR_BPF_RODATA:
                return "bpf .rodata map missing or unwritable";

            case xERR_BPF_LOAD:
                return "bpf_object__load failed";

            case xERR_BPF_FIND:
                return "bpf program or map not found by name";

            case xERR_ETCD_JSON:
                return "etcd response is not valid json";

            case xERR_ETCD_RSP:
                return "etcd response missing field or invalid";

            default:
                return "unknown internal error";
        }
    }

    // ------------------------- 正数: 上线的码 -------------------------
    switch (ec) {
        // ---- 1000+ 服务之间 ----
        case SERR_MISMATCH:
            return "payload conv/ip mismatches gateway meta";

        case SERR_CONFLICT:
            return "state conflict";

        // ---- 1xxxx 终端离开 ----
        case PERR_TER_LEAVE:
            return "terminal activate leave";

        case PERR_TER_DISCONNECTED:
            return "terminal disconnected";

        case PERR_TER_KICKED:
            return "terminal has kicked";

        case PERR_TER_PROTO_ERR:
            return "invalid protocol data";

        case PERR_TER_REJECTED:
            return "router rejected";

        case PERR_TER_GW_LOST:
            return "gateway has losted";

        case PERR_TER_TAKEOVER:
            return "account logged in on another device";

        case PERR_TER_BANNED:
            return "account banned";

        case PERR_TER_ADMIN_KICK:
            return "kicked by administrator";

        // ---- 2xxxx 请求失败 ----
        case PERR_REQ_UNREACHABLE:
            return "destination unreachable";

        case PERR_REQ_NOT_ACCEPT:
            return "destination does not accept this pid";

        default:
            break;
    }

    // 没命中具体码: 按号段回一个能定位到"哪一层,是业务码还是根本没同步"的说法.
    // 从高到低判, 命中即返回.
    if (ec >= PERR_REQ_CUSTOM) {
        return "custom request error";
    }

    if (ec >= PERR_REQ_UNREACHABLE) {
        return "unknown request error";
    }

    if (ec >= PERR_TER_CUSTOM) {
        return "custom terminal code";
    }

    if (ec >= PERR_TER_LEAVE) {
        return "unknown terminal code";
    }

    if (ec >= SERR_CUSTOM) {
        return "custom service error";
    }

    if (ec >= SERR_MISMATCH) {
        return "unknown service error";
    }

    // 正数但落在所有号段之外 -- 多半是有人把旧的小数字码传了进来
    return "not a valid error code";
}
