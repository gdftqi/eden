package com

import (
	"fmt"
	"time"

	"github.com/eva/mid"
	"github.com/google/uuid"
)

// RateRule 两级滑动窗口 + 封禁
type RateRule struct {
	Win    int `yaml:"win"`     // 短窗口长度(秒)
	Max    int `yaml:"max"`     // 短窗口内允许的次数
	Ban    int `yaml:"ban"`     // 触发短窗口后封禁多久(秒)
	DayMax int `yaml:"day_max"` // 24 小时内允许的次数
	DayBan int `yaml:"day_ban"` // 触发 24 小时上限后封禁多久(秒)
}

// Check 校验规则合法性。限速配置写错(比如 max 填 0)会把所有人挡在门外,
// 所以宁可启动就失败, 别等线上才发现。
func (this_ *RateRule) Check(name string) error {
	if this_ == nil {
		return fmt.Errorf("%s is missing", name)
	}

	if this_.Win <= 0 || this_.Max <= 0 || this_.Ban <= 0 {
		return fmt.Errorf("%s.win/max/ban 必须为正数", name)
	}

	if this_.DayMax <= 0 || this_.DayBan <= 0 {
		return fmt.Errorf("%s.day_max/day_ban 必须为正数", name)
	}

	// 短窗口上限比日上限还宽的话, 短窗口那条永远轮不到触发, 等于白配
	if this_.Max > this_.DayMax {
		return fmt.Errorf("%s.max(%d) 不应大于 day_max(%d)", name, this_.Max, this_.DayMax)
	}

	return nil
}

// 长窗口固定 24 小时
const rateDayMs = 24 * 60 * 60 * 1000

// 判定 + 记账必须原子完成, 否则多实例并发下会各自读到"还没超", 一起放行。
// Eva 是横向扩展的, 这一点是承重的, 别拆成 GET/SET 两步。
//
// KEYS[1] 封禁标记  KEYS[2] 短窗口 zset  KEYS[3] 日窗口 zset
// ARGV[1] 当前毫秒  ARGV[2] 本次的唯一成员
// ARGV[3] 短窗口毫秒  ARGV[4] 短窗口上限  ARGV[5] 短窗口封禁秒
// ARGV[6] 日窗口毫秒  ARGV[7] 日窗口上限  ARGV[8] 日窗口封禁秒
// 返回 0 = 放行; >0 = 本次被拒, 值为剩余封禁秒数
const rateHitScript = `
local ttl = redis.call('TTL', KEYS[1])
if ttl > 0 then return ttl end

local now = tonumber(ARGV[1])

-- 滑动窗口: 先把窗口外的记录裁掉, 剩下的条数就是窗口内的次数
local function used(key, win)
  redis.call('ZREMRANGEBYSCORE', key, '-inf', now - win)
  return tonumber(redis.call('ZCARD', key))
end

if used(KEYS[2], tonumber(ARGV[3])) >= tonumber(ARGV[4]) then
  redis.call('SET', KEYS[1], 1, 'EX', ARGV[5])
  return tonumber(ARGV[5])
end

if used(KEYS[3], tonumber(ARGV[6])) >= tonumber(ARGV[7]) then
  redis.call('SET', KEYS[1], 1, 'EX', ARGV[8])
  return tonumber(ARGV[8])
end

-- 放行才记账: 被封期间的请求不计数, 否则封禁会被自己不断续期
redis.call('ZADD', KEYS[2], now, ARGV[2])
redis.call('PEXPIRE', KEYS[2], ARGV[3])
redis.call('ZADD', KEYS[3], now, ARGV[2])
redis.call('PEXPIRE', KEYS[3], ARGV[6])
return 0
`

// 只查封禁状态, 不计数。用在 bcrypt 之前 —— 限速器必须比它保护的东西便宜,
// bcrypt cost 10 一次约 100ms CPU, 而这里只是一次 Redis 往返。
//
// 返回最大剩余封禁秒数(0 = 都没被封)
const rateBannedScript = `
local m = 0
for i = 1, #KEYS do
  local t = redis.call('TTL', KEYS[i])
  if t > m then m = t end
end
return m
`

func rateKeys(name, id string) []string {
	return []string{
		"rl:b:" + name + ":" + id, // 封禁标记
		"rl:s:" + name + ":" + id, // 短窗口
		"rl:d:" + name + ":" + id, // 日窗口
	}
}

// RateBanKey 封禁标记的键, 供 RateBanned 批量查询
func RateBanKey(name, id string) string {
	return "rl:b:" + name + ":" + id
}

// RateBanned 查询若干条线是否处于封禁中, 返回最大剩余秒数(0 = 放行)。
// 不计数, 可安全地放在校验流程的最前面。
func RateBanned(banKeys ...string) (int, error) {
	v, err := mid.RedisEval(rateBannedScript, banKeys)
	if err != nil {
		return 0, err
	}

	n, _ := v.(int64)
	return int(n), nil
}

// RateHit 记一次并判定是否放行. 返回 0 = 放行; > 0 = 本次被拒, 值为剩余封禁秒数
func RateHit(name, id string, r *RateRule) (int, error) {
	keys := rateKeys(name, id)
	now := time.Now().UnixMilli()

	// 唯一成员: 同一毫秒内的两次请求不能在 zset 里互相覆盖, 否则会少计
	member, err := uuid.NewRandom()
	if err != nil {
		return 0, err
	}

	v, err := mid.RedisEval(rateHitScript, keys,
		now, member.String(),
		r.Win*1000, r.Max, r.Ban,
		rateDayMs, r.DayMax, r.DayBan)
	if err != nil {
		return 0, err
	}

	n, _ := v.(int64)
	return int(n), nil
}
