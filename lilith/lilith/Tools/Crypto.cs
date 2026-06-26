using Org.BouncyCastle.Crypto.Digests;
using Org.BouncyCastle.Crypto.Macs;
using Org.BouncyCastle.Crypto.Modes;
using Org.BouncyCastle.Crypto.Parameters;
using Org.BouncyCastle.Math.EC.Rfc7748;
using System;
using System.Security.Cryptography;
using System.Text;

namespace lilith.Tools
{
    public static class Crypto
    {
        // ---- envelope MAC ----
        public const int ENVELOPE_MAC_LEN = 8;
        public const int ENVELOPE_MAC_HASH_LEN = 24;
        public static readonly byte[] SIPHASH_KEY = System.Text.Encoding.ASCII.GetBytes("XA1,y9Mn]0+iu2Y9");

        // ---- ChaCha20-Poly1305 AEAD (IETF) ----
        public const int AEAD_KEY_LEN = 32;
        public const int AEAD_NONCE_LEN = 12;
        public const int XX20_TAG_LEN = 16;

        // ---- 方向标记 ----
        public const byte DIR_C2S = 0;
        public const byte DIR_S2C = 1;

        // X25519 密钥对
        public static readonly byte[] CLI_SK = Convert.FromBase64String("EdJdIDQFPrLTOP7ppHoZi3VOrFqVWKG/e02D5pCn5IA=");
        public static readonly byte[] CLI_PK = Convert.FromBase64String("IeXygWC1oAuSDeZp76WiWTkAj/VvWqs+NJ043/bG2Bo=");

        public static byte[] Token(string b64Token)
        {
            return Convert.FromBase64String(b64Token);
        }

        public static byte[] SipHashTag(byte[] data, int offset, int len)
        {
            var mac = new SipHash();
            mac.Init(new KeyParameter(SIPHASH_KEY));
            mac.BlockUpdate(data, offset, len);
            long h = mac.DoFinal();
            var tag = new byte[ENVELOPE_MAC_LEN];
            for (int i = 0; i < 8; i++)
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

        public static int Encrypt(byte[] key, byte[] nonce, byte[] inBuf, int inOff, int inLen, byte[] outBuf, int outOff)
        {// ChaCha20-Poly1305 加密
            var aead = new ChaCha20Poly1305();
            aead.Init(true, new AeadParameters(new KeyParameter(key), XX20_TAG_LEN * 8, nonce, null));
            int len = aead.ProcessBytes(inBuf, inOff, inLen, outBuf, outOff);
            len += aead.DoFinal(outBuf, outOff + len);
            return len;
        }

        public static int Decrypt(byte[] key, byte[] nonce, byte[] inBuf, int inOff, int inLen, byte[] outBuf, int outOff)
        {// ChaCha20-Poly1305 解密
            if (inLen < XX20_TAG_LEN)
            {
                return -1;
            }

            var aead = new ChaCha20Poly1305();
            aead.Init(false, new AeadParameters(new KeyParameter(key), XX20_TAG_LEN * 8, nonce, null));
            try
            {
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

        public static void KxClient(byte[] srvPk, out byte[] rx, out byte[] tx)
        {// X25519 交换密钥
            var q = new byte[32];
            X25519.ScalarMult(CLI_SK, 0, srvPk, 0, q, 0);

            var blake = new Blake2bDigest(512);
            blake.BlockUpdate(q, 0, 32);
            blake.BlockUpdate(CLI_PK, 0, 32);
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
                return BitConverter.ToString(sha256.ComputeHash(inputBytes)).Replace("-", "").ToLowerInvariant();
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