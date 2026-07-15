package mid

import (
	"context"
	"time"

	"github.com/eva/log"
	clientv3 "go.etcd.io/etcd/client/v3"
	etcd3 "go.etcd.io/etcd/client/v3"
)

const ETCD_TIMEOUT = time.Second * 5

// EtcdConfig etcd 配置
type EtcdConfig struct {
	Hosts []string `yaml:"hosts"          json:"hosts"` // etcd 服务地址列表，例: ["127.0.0.1:2379"]
	User  string   `yaml:"user,omitempty" json:"-"`     // 用户名（可选）
	Pass  string   `yaml:"pass,omitempty" json:"-"`     // 密码（可选）
}

// Etcd etcd 客户端单例
var Etcd *etcd3.Client

// InitEtcd 初始化 etcd 客户端并创建单例对象
// conf: etcd 配置
func InitEtcd(conf *EtcdConfig) error {
	c, err := etcd3.New(etcd3.Config{
		Endpoints:   conf.Hosts,
		Username:    conf.User,
		Password:    conf.Pass,
		DialTimeout: ETCD_TIMEOUT,
	})
	if err != nil {
		return err
	}

	if Etcd != nil {
		Etcd.Close()
	}

	Etcd = c

	return nil
}

// EtcdGetPrefix 获取 etcd 中的 key 值
// key: 要获取的 key
func EtcdGetPrefix(key string) (*etcd3.GetResponse, error) {
	log.ASSERT(Etcd != nil, "ETCD 未初始化")

	ctx, cancel := context.WithTimeout(context.TODO(), ETCD_TIMEOUT)
	defer cancel()

	return Etcd.Get(ctx, key, clientv3.WithPrefix())
}

// EtcdDelete 删除 etcd 中的 key
// key: 要删除的 key
func EtcdDelete(key string) (*etcd3.DeleteResponse, error) {
	log.ASSERT(Etcd != nil, "ETCD 未初始化")

	ctx, cancel := context.WithTimeout(context.TODO(), ETCD_TIMEOUT)
	defer cancel()

	return Etcd.Delete(ctx, key)
}

// EtcdPut 设置 etcd 中的 key 值
// key: 要设置的 key
// value: 要设置的值
// opts: 可选的操作选项（如 WithLease 等）
func EtcdPut(key, value string, opts ...etcd3.OpOption) (*etcd3.PutResponse, error) {
	log.ASSERT(Etcd != nil, "ETCD 未初始化")

	ctx, cancel := context.WithTimeout(context.TODO(), ETCD_TIMEOUT)
	defer cancel()

	return Etcd.Put(ctx, key, value, opts...)
}

// EtcdGrant 申请 etcd 租约
// ttl: 租约的生存时间（秒）
// 返回租约ID，可用于自动续租
func EtcdGrant(ttl int64) (*etcd3.LeaseGrantResponse, error) {
	log.ASSERT(Etcd != nil, "ETCD 未初始化")

	ctx, cancel := context.WithTimeout(context.TODO(), ETCD_TIMEOUT)
	defer cancel()

	return Etcd.Grant(ctx, ttl)
}

// EtcdKeepAliveOnce 续租一次
// id: 租约ID
// 用于手动续租，通常用于保活机制
func EtcdKeepAliveOnce(id etcd3.LeaseID) (*etcd3.LeaseKeepAliveResponse, error) {
	log.ASSERT(Etcd != nil, "ETCD 未初始化")

	ctx, cancel := context.WithTimeout(context.TODO(), ETCD_TIMEOUT)
	defer cancel()

	return Etcd.KeepAliveOnce(ctx, id)
}

// EtcdWatchWithContext 监听某个 key 的变化
// ctx: 上下文，用于控制监听的生命周期
// key: 要监听的 key（支持前缀匹配）
// opts: 可选的操作选项
// 返回一个 WatchChan，用于接收变化事件
func EtcdWatchWithContext(ctx context.Context, key string, opts ...etcd3.OpOption) etcd3.WatchChan {
	log.ASSERT(Etcd != nil, "ETCD 未初始化")
	return Etcd.Watch(ctx, key, opts...)
}

// EtcdPutIfAbsentWithLease 使用事务原子注册：当且仅当 key 不存在时，携带租约写入
// 用于实现分布式锁和服务注册，确保同一服务只注册一次
// key: 要注册的 key
// value: 要注册的值
// ttl: 租约的生存时间（秒）
// 返回：leaseID（成功时有效）、是否成功写入（true 表示抢占成功）、错误
func EtcdPutIfAbsentWithLease(key, value string, ttl int64) (etcd3.LeaseID, bool, error) {
	log.ASSERT(Etcd != nil, "ETCD 未初始化")

	ctx, cancel := context.WithTimeout(context.TODO(), ETCD_TIMEOUT)
	defer cancel()

	grant, err := Etcd.Grant(ctx, ttl)
	if err != nil {
		return 0, false, err
	}

	txn := Etcd.Txn(ctx).If(
		etcd3.Compare(etcd3.Version(key), "=", 0),
	).Then(
		etcd3.OpPut(key, value, etcd3.WithLease(grant.ID)),
	).Else()

	rsp, err := txn.Commit()
	if err != nil {
		// 出错回收租约，避免泄漏
		rctx, rcancel := context.WithTimeout(context.TODO(), ETCD_TIMEOUT)
		_, _ = Etcd.Revoke(rctx, grant.ID)
		rcancel()
		return 0, false, err
	}

	if !rsp.Succeeded {
		// 抢占失败，同名实例已存在，回收本次租约
		rctx, rcancel := context.WithTimeout(context.TODO(), ETCD_TIMEOUT)
		_, _ = Etcd.Revoke(rctx, grant.ID)
		rcancel()
		return 0, false, nil
	}

	return grant.ID, true, nil
}
