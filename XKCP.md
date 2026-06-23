XKCP 是 KCP的扩展，所以XKCP完美的继随了 KCP除了流模式的所有特性。
XKCP 要求必需握手之后才能正常传输数据。
所以 XKCP 的流程是:
1, 客户端发起SYNC，带上TOKEN。
2, 服务端回应SACK。
3，正常的处理其他KCP 其他CMD。
但是如果 XKCP 的 valid 字段 == 0 的时候，除了 SYNC其他任何CMD都不处理。

update中添加了超时检测，会主动发送ping。但是只有客户端的一端会主动发。
而服务端的一侧只是检查XKCP的状态和重传消息和ACK。

如果XKCP 的 valid 大于 5，XKCP 被认为无效。
如果XKCP 的 last_rcv_ms 超过 timeout，被认为无效。
如果XKCP 某个 segment XMIT 超过 5也被认为无效。

XKCP 的 state 表示有效或无效，state == 0 有效, state == (uint32_t)-1 无效

作为服务端不应该收到 PONG，SACK，RST
作为客户端不应该收到 PING, SYNC