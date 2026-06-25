package mid

import (
	"context"
	"time"

	"github.com/redis/go-redis/v9"
)

// Redis 客户端
var Redis *redis.Client

// Redis 超时时间
const REDIS_TIMEOUT = time.Second * 5

// Redis 配置
type RedisConfig struct {
	Addr     string `yaml:"addr"`     // Redis 地址
	Username string `yaml:"username"` // Redis 用户名
	Password string `yaml:"password"` // Redis 密码
}

// InitRedis 初始化 Redis 客户端
// c: Redis 配置
func InitRedis(c *RedisConfig) error {
	const timeout = time.Second * 15

	r := redis.NewClient(&redis.Options{
		Addr:            c.Addr,
		Username:        c.Username,
		Password:        c.Password,
		DialTimeout:     timeout,
		ConnMaxIdleTime: -1,
	})

	ctx, cancel := context.WithTimeout(context.TODO(), timeout)
	defer cancel()

	err := r.Ping(ctx).Err()
	if err != nil {
		r.Close()
		return err
	}

	Redis = r
	return nil
}

// RedisSetEx 设置键值并设置过期时间
// key: 键
// value: 值
// ttl: 过期时间
func RedisSetEx(key string, value interface{}, ttl time.Duration) error {
	ctx, cancel := context.WithTimeout(context.Background(), REDIS_TIMEOUT)
	defer cancel()
	return Redis.Set(ctx, key, value, ttl).Err()
}

// RedisSet 设置键值
// key: 键
// value: 值
func RedisSet(key string, value interface{}) error {
	ctx, cancel := context.WithTimeout(context.Background(), REDIS_TIMEOUT)
	defer cancel()
	return Redis.Set(ctx, key, value, 0).Err()
}

// RedisGet 获取键值
// key: 键
// 返回: 值, 错误
func RedisGet(key string) (string, error) {
	ctx, cancel := context.WithTimeout(context.Background(), REDIS_TIMEOUT)
	defer cancel()
	str, err := Redis.Get(ctx, key).Result()
	if err != nil {
		if err == redis.Nil {
			return "", nil
		}
		return "", err
	}

	return str, nil
}

// RedisSetNx 设置键值并设置过期时间, 如果键不存在则设置
// key: 键
// value: 值
// ttl: 过期时间
// 返回: 是否设置成功, 错误
func RedisSetNx(key string, value interface{}, ttl time.Duration) (bool, error) {
	ctx, cancel := context.WithTimeout(context.Background(), REDIS_TIMEOUT)
	defer cancel()
	res, err := Redis.SetNX(ctx, key, value, ttl).Result()
	if err != nil {
		return false, err
	}

	return res, nil
}

// RedisDel 删除键
// key: 键
// 返回: 错误
func RedisDel(key string) error {
	ctx, cancel := context.WithTimeout(context.Background(), REDIS_TIMEOUT)
	defer cancel()
	return Redis.Del(ctx, key).Err()
}

// RedisKeys 根据模式获取所有匹配的 key
// pattern: 匹配模式，如 "__#GameRoom_0x00000001_*"
// 返回: key 列表, 错误
func RedisKeys(pattern string) ([]string, error) {
	ctx, cancel := context.WithTimeout(context.Background(), REDIS_TIMEOUT)
	defer cancel()
	return Redis.Keys(ctx, pattern).Result()
}

// RedisEval 执行 Lua 脚本，一次往返获取所有匹配 key 的值
// script: Lua 脚本
// keys: KEYS 参数（可为空）
// args: ARGV 参数
// 返回: 脚本返回值, 错误
func RedisEval(script string, keys []string, args ...interface{}) (interface{}, error) {
	ctx, cancel := context.WithTimeout(context.Background(), REDIS_TIMEOUT)
	defer cancel()
	return Redis.Eval(ctx, script, keys, args...).Result()
}
