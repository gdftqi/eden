using Avalonia;
using Avalonia.Controls;
using CC.Eva;
using CC.Model;
using CC.Proto;
using Lilith.Utils;
using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace CC
{
    public partial class ChatTab : BaseTab
    {
        public event Action<SingleChatReq>? SendRequested;

        // 当前打开的会话
        private ChatConversation? current;

        // 只加载一次: 页签常驻可视树(靠 IsVisible 切), 进出会重复触发 attach
        private bool loaded;

        public ChatTab()
        {
            InitializeComponent();
            ChatPanel.ChatSelected += OpenChat;
            ChatView.SendRequested += OnSendRequested;
        }


        // 聊天窗要发消息
        private void OnSendRequested(SingleChatReq req)
        {
            SendRequested?.Invoke(req);

            if (current != null)
            {
                long now = DateTimeOffset.Now.ToUnixTimeMilliseconds();
                current.LastPreview = req.Content;
                current.LastTime = now;
                ChatPanel.Upsert(current, true);
                UpdateConversation(current.ChatId, req.Content, now, 0);
            }
        }


        protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
        {
            base.OnAttachedToVisualTree(e);

            if (!loaded)
            {
                loaded = true;
                _ = Reload();
            }
        }


        // 组织架构的 uid -> User 映射, Reload 时拉取; 收到陌生会话的推送时补名字/头像用
        private readonly Dictionary<Int64, User> orgUsers = new Dictionary<Int64, User>();

        /// <summary>
        /// 从本地库加载会话列表. 名字/头像不落会话表, 从组织架构解析.
        /// </summary>
        private async Task Reload()
        {
            if (Me.Db == null)
            {
                return;
            }

            var users = orgUsers;
            try
            {
                var rsp = await GetOrg.POST();
                foreach (var u in rsp.Users ?? new List<User>())
                {
                    if (u.ID.HasValue)
                    {
                        users[u.ID.Value] = u;
                    }
                }
            }
            catch (Exception ex)
            {
                // 拉不到就先用占位名显示, 会话本身不受影响
                Log.Write($"[ChatTab] 拉取组织架构失败: {ex.Message}");
            }

            try
            {
                using var cmd = Me.Db.CreateCommand();
                // 升序读 + Upsert 插顶 = 最后时间最新的排最上
                cmd.CommandText = @"
SELECT f_chat_id, f_peer_id, f_recv_seq, f_read_seq, f_peer_read_seq,
       f_unread, f_last_preview, f_last_time
FROM t_chat_conversation ORDER BY f_last_time ASC";

                using var r = cmd.ExecuteReader();
                while (r.Read())
                {
                    var conv = new ChatConversation
                    {
                        ChatId      = r.GetInt64(0),
                        PeerId      = r.GetInt64(1),
                        RecvSeq     = r.GetInt64(2),
                        ReadSeq     = r.GetInt64(3),
                        PeerReadSeq = r.GetInt64(4),
                        Unread      = r.GetInt32(5),
                        LastPreview = r.IsDBNull(6) ? string.Empty : r.GetString(6),
                        LastTime    = r.GetInt64(7),
                    };

                    if (users.TryGetValue(conv.PeerId, out var u))
                    {
                        conv.PeerName   = u.Nickname ?? string.Empty;
                        conv.PeerAvatar = u.Avatar ?? string.Empty;
                    }
                    else
                    {
                        conv.PeerName = $"用户 {conv.PeerId}";
                    }

                    ChatPanel.Upsert(conv);
                }
            }
            catch (Exception ex)
            {
                Log.Write($"[ChatTab] 会话加载失败: {ex.Message}");
            }
        }


        protected override void OnNotify(Message msg)
        {
            switch (msg.ID)
            {
                case MsgID.OpenChat:
                    if (msg.Param is User peer)
                    {
                        OpenConversation(peer);
                    }
                    break;
            }
        }


        // 打开(必要时创建)与 peer 的单聊会话: 落库 -> 进列表 -> 选中
        private void OpenConversation(User peer)
        {
            if (peer.ID == null)
            {
                return;
            }

            Int64 chatId = ChatConversation.MakeChatId(Me.ID, peer.ID.Value);

            // 列表里已有就用它挂着的那份(消息都在上面), 没有才新建
            var conv = ChatPanel.Find(chatId)?.Conversation;
            if (conv == null)
            {
                conv = new ChatConversation
                {
                    ChatId     = chatId,
                    PeerId     = peer.ID.Value,
                    PeerName   = peer.Nickname ?? string.Empty,
                    PeerAvatar = peer.Avatar ?? string.Empty,
                };

                InsertConversation(conv);
            }

            ChatPanel.Select(ChatPanel.Upsert(conv));
        }


        // 未读清零落库
        private static void ClearUnread(long chatId)
        {
            if (Me.Db == null)
            {
                return;
            }

            try
            {
                using var cmd = Me.Db.CreateCommand();
                cmd.CommandText = "UPDATE t_chat_conversation SET f_unread = 0 WHERE f_chat_id = $cid";
                cmd.Parameters.AddWithValue("$cid", chatId);
                cmd.ExecuteNonQuery();
            }
            catch (Exception ex)
            {
                Log.Write($"[ChatTab] 未读清零失败: {ex.Message}");
            }
        }


        // 会话落库(幂等): 已存在就什么都不做
        private static void InsertConversation(ChatConversation conv)
        {
            if (Me.Db == null)
            {
                return;
            }

            try
            {
                using var cmd = Me.Db.CreateCommand();
                cmd.CommandText = "INSERT OR IGNORE INTO t_chat_conversation(f_chat_id, f_peer_id) VALUES ($cid, $pid)";
                cmd.Parameters.AddWithValue("$cid", conv.ChatId);
                cmd.Parameters.AddWithValue("$pid", conv.PeerId);
                cmd.ExecuteNonQuery();
            }
            catch (Exception ex)
            {
                Log.Write($"[ChatTab] 会话落库失败: {ex.Message}");
            }
        }


        private void OpenChat(ChatItem item)
        {
            current = item.Conversation;
            ChatView.Conversation = item.Conversation;

            // 点开即已读: 角标熄灭 + 落库清零
            if (current != null && current.Unread > 0)
            {
                current.Unread = 0;
                item.Unread = 0;
                ClearUnread(current.ChatId);
            }

            ChatView.PeerName = item.Nickname;
            ChatView.PeerAvatar = item.Avatar;
            ChatView.PeerStatus = string.Empty;

            ChatView.ClearMessages();
            EmptyState.IsVisible = false;
            ChatView.IsVisible = true;
        }

        // 服务端来的消息进聊天窗(MainWindow.OnPackage 转发过来)
        public void AddText(bool mine, string text, string time)
        {
            ChatView.AddText(mine, text, time);
        }


        // 对端消息(MainWindow.OnChatNtf 已把会话+消息落库后转发).
        // 正开着这个会话: 出气泡, 不计未读; 否则(含列表里还没有这个会话):
        // 找/建列表行 + 未读加一, 但不打开聊天窗, 不抢用户当前的操作
        public void OnPeerMessage(long chatId, long fromId, string content, long createdAt)
        {
            if (current != null && current.ChatId == chatId)
            {
                ChatView.AddText(false, content,
                    DateTimeOffset.FromUnixTimeMilliseconds(createdAt).LocalDateTime.ToString("HH:mm"));

                current.LastPreview = content;
                current.LastTime = createdAt;
                ChatPanel.Upsert(current, true);
                UpdateConversation(chatId, content, createdAt, 0);
                return;
            }

            var conv = ChatPanel.Find(chatId)?.Conversation;
            if (conv == null)
            {
                conv = new ChatConversation
                {
                    ChatId = chatId,
                    PeerId = fromId,
                };

                if (orgUsers.TryGetValue(fromId, out var u))
                {
                    conv.PeerName   = u.Nickname ?? string.Empty;
                    conv.PeerAvatar = u.Avatar ?? string.Empty;
                }
                else
                {
                    conv.PeerName = $"用户 {fromId}";
                }
            }

            conv.Unread += 1;
            conv.LastPreview = content;
            conv.LastTime = createdAt;
            ChatPanel.Upsert(conv, true);
            UpdateConversation(chatId, content, createdAt, 1);
        }


        // 会话表的预览/时间/未读增量落库(会话行由 OnChatNtf 保证已存在)
        private static void UpdateConversation(long chatId, string preview, long time, int unreadDelta)
        {
            if (Me.Db == null)
            {
                return;
            }

            try
            {
                using var cmd = Me.Db.CreateCommand();
                cmd.CommandText =
                    "UPDATE t_chat_conversation SET f_unread = f_unread + $d, f_last_preview = $p, f_last_time = $t " +
                    "WHERE f_chat_id = $cid";
                cmd.Parameters.AddWithValue("$d", unreadDelta);
                cmd.Parameters.AddWithValue("$p", preview);
                cmd.Parameters.AddWithValue("$t", time);
                cmd.Parameters.AddWithValue("$cid", chatId);
                cmd.ExecuteNonQuery();
            }
            catch (Exception ex)
            {
                Log.Write($"[ChatTab] 会话表更新失败: {ex.Message}");
            }
        }
    }
}
