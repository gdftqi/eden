using System;
using Org.BouncyCastle.Crypto.Digests;
using Org.BouncyCastle.Crypto.Macs;
using Org.BouncyCastle.Crypto.Modes;
using Org.BouncyCastle.Crypto.Parameters;
using Org.BouncyCastle.Math.EC.Rfc7748;


namespace Echidna
{
    // =========================================================================
    //        typhon 客户端密码学栈 (BouncyCastle) —— 对齐 C++ utils/cryptor
    //                          + examples/kcp_echo/test_kcp.py
    // =========================================================================
    //  三层:
    //   1. SipHash-2-4 envelope MAC  —— 8B tag prepend 在 KCP frame 前(XDP 校验)
    //   2. ChaCha20-Poly1305 AEAD    —— payload 加密, 12B nonce, 16B tag 附尾
    //   3. X25519 ECDH + BLAKE2b KDF —— 握手派生会话密钥(crypto_kx_client)
    // =========================================================================
    public static class Crypto
    {
        // ---- envelope MAC (SipHash-2-4) ----
        public const int ENVELOPE_MAC_LEN      = 8;   // 8B tag, prepend 在 KCP frame 前
        public const int ENVELOPE_MAC_HASH_LEN = 24;  // MAC 只覆盖 KCP frame 前 24B (KCP header)

        // SipHash key, 必须与服务端 kcp.siphash (config.yml) 完全一致;
        // 明文 16 字符直接当字节(C++ 端 memcpy, 不 base64)。
        public static readonly byte[] SH_KEY =
            System.Text.Encoding.ASCII.GetBytes("XA1,y9Mn]0+iu2Y9");

        // ---- ChaCha20-Poly1305 AEAD (IETF) ----
        public const int AEAD_KEY_LEN   = 32;
        public const int AEAD_NONCE_LEN = 12;
        public const int XX20_TAG_LEN   = 16;

        // ---- 方向标记 (与 C++ session.cpp / test_kcp.py 一致) ----
        public const byte DIR_C2S = 0;  // client→server 上行: 客户端 send 加密用 tx_key
        public const byte DIR_S2C = 1;  // server→client 下行: 客户端 recv 解密用 rx_key

        // ---- 写死鉴权材料 (与 test_kcp.py 一致, gen_tokens 预生成) ----
        // 固定 client X25519 密钥对: 私钥握手做 ECDH; 公钥已被登录服签进每个 token。
        public static readonly byte[] CLI_SK =
            Convert.FromBase64String("EdJdIDQFPrLTOP7ppHoZi3VOrFqVWKG/e02D5pCn5IA=");
        public static readonly byte[] CLI_PK =
            Convert.FromBase64String("IeXygWC1oAuSDeZp76WiWTkAj/VvWqs+NJ043/bG2Bo=");

        // token 绑定 conv: 第 i 个对应 conv = 2000 + i; 有效期 10 年。
        public const uint CONV_BASE = 2000;
        public static readonly string[] TOKENS_B64 =
        {
            "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",
        };

        public static byte[] Token(uint conv) => Convert.FromBase64String(TOKENS_B64[conv]);


        // ---------------------------------------------------------------------
        // SipHash-2-4 envelope tag —— 输出 8B little-endian (与 C++ htole64(siphash24) 一致)
        // ---------------------------------------------------------------------
        public static byte[] SipHashTag(byte[] data, int len) => SipHashTag(data, 0, len);

        // offset 重载: 对 data[offset .. offset+len) 算 tag。
        // 出站从 0 起(KCP frame 在 buf 开头); 入站从 8 起(跳过收到的 MAC, 只覆盖 KCP frame)。
        public static byte[] SipHashTag(byte[] data, int offset, int len)
        {
            var mac = new SipHash();                 // 默认 c=2 d=4
            mac.Init(new KeyParameter(SH_KEY));
            mac.BlockUpdate(data, offset, len);
            long h = mac.DoFinal();                  // host-order 64-bit
            var tag = new byte[ENVELOPE_MAC_LEN];
            for (int i = 0; i < 8; i++)
                tag[i] = (byte)(h >> (8 * i));       // little-endian
            return tag;
        }


        // ---------------------------------------------------------------------
        // 12B nonce = conv(4 LE) | seq(4 LE) | dir(1) | 0(3), 与 C++ make_nonce 一致
        // ---------------------------------------------------------------------
        public static byte[] MakeNonce(uint conv, uint seq, byte dir)
        {
            var n = new byte[AEAD_NONCE_LEN];
            n[0] = (byte)conv;        n[1] = (byte)(conv >> 8);
            n[2] = (byte)(conv >> 16); n[3] = (byte)(conv >> 24);
            n[4] = (byte)seq;         n[5] = (byte)(seq >> 8);
            n[6] = (byte)(seq >> 16);  n[7] = (byte)(seq >> 24);
            n[8] = dir;               // n[9..12] = 0
            return n;
        }


        // ---------------------------------------------------------------------
        // ChaCha20-Poly1305 IETF 加密: 输出 密文 + 16B tag (tag 附尾), 无 AAD。
        // ---------------------------------------------------------------------
        public static byte[] Encrypt(byte[] key, byte[] nonce, byte[] plain, int plainLen)
        {
            var aead = new ChaCha20Poly1305();
            aead.Init(true, new AeadParameters(new KeyParameter(key), XX20_TAG_LEN * 8, nonce, null));
            var outBuf = new byte[aead.GetOutputSize(plainLen)];   // = plainLen + 16
            int n = aead.ProcessBytes(plain, 0, plainLen, outBuf, 0);
            aead.DoFinal(outBuf, n);                               // 附加 16B tag
            return outBuf;
        }


        // ---------------------------------------------------------------------
        // ChaCha20-Poly1305 IETF 解密: body = 密文 + 16B tag; 验签失败返回 null。
        // ---------------------------------------------------------------------
        public static byte[]? Decrypt(byte[] key, byte[] nonce, byte[] body, int bodyLen)
        {
            if (bodyLen < XX20_TAG_LEN)
            {
                return null;
            }

            var aead = new ChaCha20Poly1305();
            aead.Init(false, new AeadParameters(new KeyParameter(key), XX20_TAG_LEN * 8, nonce, null));
            var outBuf = new byte[aead.GetOutputSize(bodyLen)];    // = bodyLen - 16
            try
            {
                int n = aead.ProcessBytes(body, 0, bodyLen, outBuf, 0);
                aead.DoFinal(outBuf, n);                           // 验证 tag, 失败抛异常
                return outBuf;
            }
            catch
            {
                return null;                                       // tag 验证失败(被篡改/密钥错)
            }
        }


        // ---------------------------------------------------------------------
        // crypto_kx_client: 返回 (rx, tx) 各 32B; 与 libsodium / test_kcp.py 位等价。
        //   q  = X25519(CLI_SK, srvPk)
        //   h  = BLAKE2b-512(q || CLI_PK || srvPk)
        //   rx = h[0..32)   (收方向, == server 的 tx)
        //   tx = h[32..64)  (发方向, == server 的 rx)
        // ---------------------------------------------------------------------
        public static void KxClient(byte[] srvPk, out byte[] rx, out byte[] tx)
        {
            var q = new byte[32];
            X25519.ScalarMult(CLI_SK, 0, srvPk, 0, q, 0);

            var blake = new Blake2bDigest(512);      // 512 bits = 64 bytes
            blake.BlockUpdate(q,      0, 32);
            blake.BlockUpdate(CLI_PK, 0, 32);
            blake.BlockUpdate(srvPk,  0, 32);
            var h = new byte[64];
            blake.DoFinal(h, 0);

            rx = new byte[32];
            tx = new byte[32];
            Array.Copy(h, 0,  rx, 0, 32);
            Array.Copy(h, 32, tx, 0, 32);
        }
    }
}
