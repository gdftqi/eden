package com

import "github.com/eva/mid"

// convSeqKey Redis 里全局单调 conv 序号的键。
const convSeqKey = "TYPHON_CONV_SEQ"

// MakeConv 生成 kcp conv, 满足:
//
//  1. conv % nthread == userID % nthread —— 与网关 sk_reuseport 的 conv%nthread 分片
//     一致, 保证这条会话落在负责该 userID 的那个 kcp worker 上;
//  2. 不冲突 —— seq 由 Redis INCR 提供(全局唯一、跨副本、跨重启单调),
//     同一时刻绝不会有两条会话拿到同一 conv。
//
// conv 落在 [0, kmax*nthread) 内(kmax = 2^32/nthread), 绝不触及 uint32 回绕, 故残差
// 不变式对【任意】nthread 精确成立。conv 空间随序号轮转, 轮回周期
// T ≈ 2^32 / (nthread × 每秒登录数), 远大于会话存活期(会话 Redis TTL 20 分钟),
// 因此活跃会话之间永不撞号。
func MakeConv(userID, nthread uint32) (uint32, error) {
	seq, err := mid.RedisIncr(convSeqKey)
	if err != nil {
		return 0, err
	}

	if nthread <= 1 {
		// 单 worker: conv%1==0 恒成立, 无残差约束, 直接用序号(避开 0)。
		conv := uint32(seq)
		if conv == 0 {
			conv = 1
		}
		return conv, nil
	}

	kmax := (uint64(1) << 32) / uint64(nthread)
	conv := uint32((uint64(seq)%kmax)*uint64(nthread) + uint64(userID%nthread))
	if conv == 0 {
		// conv==0 被 BPF / 握手保留(见 Session::getconv)。此时必然 userID%nthread==0,
		// 顺延一整个残差步长到 nthread, 仍落在残差类 0。
		conv = nthread
	}
	return conv, nil
}
