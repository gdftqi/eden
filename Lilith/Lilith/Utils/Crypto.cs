using Org.BouncyCastle.Crypto.Digests;
using Org.BouncyCastle.Crypto.Macs;
using Org.BouncyCastle.Crypto.Modes;
using Org.BouncyCastle.Crypto.Parameters;
using Org.BouncyCastle.Math.EC.Rfc7748;
using System;
using System.Security.Cryptography;
using System.Text;

namespace Lilith.Utils
{
    public static class Crypto
    {
        // ---- envelope MAC ----
        // 信封布局(与 Adam 的 core/adam.in.hpp 逐字节对应):
        // 0 8 槽位 SipHash MAC 服务端 XDP 校验, 覆盖 [8,32)
        // 8 4 conv 明文 XDP 选密钥槽 + sk_reuseport 分流
        // 12 4 计数器 AEAD nonce + 防重放序号
        // 16 N AEAD 密文 明文 = 完整 KCP 数据报
        // .. 16 AEAD tag AAD = [8,16)
        public const int ENVELOPE_MAC_LEN = 8;
        public const int ENVELOPE_MAC_HASH_LEN = 24;
        public const int ENVELOPE_CONV_OFF = 8;
        public const int ENVELOPE_CTR_OFF = 12;
        public const int ENVELOPE_HDR_LEN = 16;
        public const int ENVELOPE_OVERHEAD = ENVELOPE_HDR_LEN + XX20_TAG_LEN;

        // ---- ChaCha20-Poly1305 AEAD (IETF) ----
        public const int AEAD_KEY_LEN = 32;
        public const int AEAD_NONCE_LEN = 12;
        public const int XX20_TAG_LEN = 16;

        // ---- 方向标记 ----
        public const byte DIR_C2S = 0;
        public const byte DIR_S2C = 1;

        public static byte[] SipHashTag(byte[] key, byte[] data, int offset, int len)
        {
            var mac = new SipHash();
            mac.Init(new KeyParameter(key));
            mac.BlockUpdate(data, offset, len);
            long h = mac.DoFinal();
            var tag = new byte[ENVELOPE_MAC_LEN];
            for (int i = 0; i < ENVELOPE_MAC_LEN; i++)
            {
                tag[i] = (byte)(h >> (8 * i));
            }

            return tag;
        }

        public static byte[] MakeNonce(uint conv, uint seq, byte dir)
        {// 12B nonce = conv(4 LE) | seq(4 LE) | dir(1) | 0(3)
            var n = new byte[AEAD_NONCE_LEN];
            n[0] = (byte)conv; n[1] = (byte)(conv >> 8);
            n[2] = (byte)(conv >> 16); n[3] = (byte)(conv >> 24);
            n[4] = (byte)seq; n[5] = (byte)(seq >> 8);
            n[6] = (byte)(seq >> 16); n[7] = (byte)(seq >> 24);
            n[8] = dir;
            return n;
        }

        public static byte[] RandomNonce()
        {// 12B 安全随机 nonce
            var n = new byte[AEAD_NONCE_LEN];
            RandomNumberGenerator.Fill(n);
            return n;
        }

        /// <param name="aad">附加认证数据(只认证不加密, 如 Package 头); 收发两侧必须完全一致</param>
        public static int Encrypt(byte[] key, byte[] nonce, byte[] inBuf, int inOff, int inLen, byte[] outBuf, int outOff,
                                  byte[]? aad = null, int aadOff = 0, int aadLen = 0)
        {// ChaCha20-Poly1305 加密
            var aead = new ChaCha20Poly1305();
            aead.Init(true, new AeadParameters(new KeyParameter(key), XX20_TAG_LEN * 8, nonce, null));
            // 用 ProcessAadBytes 而不是 AeadParameters 的 associatedText, 免去一次数组拷贝
            if (aad != null && aadLen > 0)
                aead.ProcessAadBytes(aad, aadOff, aadLen);
            int len = aead.ProcessBytes(inBuf, inOff, inLen, outBuf, outOff);
            len += aead.DoFinal(outBuf, outOff + len);
            return len;
        }

        /// <param name="aad">必须与加密侧传的完全一致, 否则验签失败</param>
        public static int Decrypt(byte[] key, byte[] nonce, byte[] inBuf, int inOff, int inLen, byte[] outBuf, int outOff,
                                  byte[]? aad = null, int aadOff = 0, int aadLen = 0)
        {// ChaCha20-Poly1305 解密
            if (inLen < XX20_TAG_LEN)
            {
                return -1;
            }

            var aead = new ChaCha20Poly1305();
            aead.Init(false, new AeadParameters(new KeyParameter(key), XX20_TAG_LEN * 8, nonce, null));
            try
            {
                if (aad != null && aadLen > 0)
                    aead.ProcessAadBytes(aad, aadOff, aadLen);
                int len = aead.ProcessBytes(inBuf, inOff, inLen, outBuf, outOff);
                len += aead.DoFinal(outBuf, outOff + len);
                return len;
            }
            catch
            {
                return -1;
            }
        }

        public static void X25519KeyGen(out byte[] pk, out byte[] sk)
        {// 生成 X25519 密钥对
            sk = new byte[32];
            RandomNumberGenerator.Fill(sk);

            var basePoint = new byte[32];
            basePoint[0] = 9;
            pk = new byte[32];
            X25519.ScalarMult(sk, 0, basePoint, 0, pk, 0);
        }

        public static void X25519KxClient(byte[] cliSk, byte[] cliPk, byte[] srvPk, out byte[] rx, out byte[] tx)
        {// X25519 交换密钥(crypto_kx client): 必须传"自己发出去那对"密钥, 否则与对端派生不出同一组 rx/tx
            var q = new byte[32];
            X25519.ScalarMult(cliSk, 0, srvPk, 0, q, 0);

            var blake = new Blake2bDigest(512);
            blake.BlockUpdate(q, 0, 32);
            blake.BlockUpdate(cliPk, 0, 32);
            blake.BlockUpdate(srvPk, 0, 32);
            var h = new byte[64];
            blake.DoFinal(h, 0);

            rx = new byte[32];
            tx = new byte[32];
            Array.Copy(h, 0, rx, 0, 32);
            Array.Copy(h, 32, tx, 0, 32);
        }

        public static string Sha256(string plainText)
        {// SHA256
            if (string.IsNullOrEmpty(plainText))
            {
                return string.Empty;
            }

            byte[] inputBytes = Encoding.UTF8.GetBytes(plainText);
            using (SHA256 sha256 = SHA256.Create())
            {
                // 大写 hex: 服务端 bcrypt 比对的是字节串, 两端大小写必须一致
                return BitConverter.ToString(sha256.ComputeHash(inputBytes)).Replace("-", "").ToUpperInvariant();
            }
        }

        public static string Base64Encode(string plainText)
        {
            if (string.IsNullOrEmpty(plainText))
            {
                return string.Empty;
            }
            return Convert.ToBase64String(Encoding.UTF8.GetBytes(plainText));
        }

        public static string Base64DecodeToString(string base64Text)
        {
            if (string.IsNullOrEmpty(base64Text))
            {
                return string.Empty;
            }
            return Encoding.UTF8.GetString(Convert.FromBase64String(base64Text));
        }

        public static string Base64Encode(byte[] bytes)
        {
            if (bytes == null || bytes.Length == 0)
            {
                return string.Empty;
            }
            return Convert.ToBase64String(bytes);
        }

        public static byte[] Base64DecodeToBytes(string base64Text)
        {
            if (string.IsNullOrEmpty(base64Text))
            {
                return Array.Empty<byte>();
            }
            return Convert.FromBase64String(base64Text);
        }
    }
}