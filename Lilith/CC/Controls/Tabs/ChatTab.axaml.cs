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


        private void OnSendRequested(SingleChatReq req)
        {
            SendRequested?.Invoke(req);
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


        private readonly Dictionary<Int64, User> orgUsers = new Dictionary<Int64, User>();


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

                    LoadMessages(conv);
                    ChatPanel.Upsert(conv);
                }
            }
            catch (Exception ex)
            {
                Log.Write($"[ChatTab] 会话加载失败: {ex.Message}");
            }
        }


        private const int HISTORY_LIMIT = 50;


        private static void LoadMessages(ChatConversation conv)
        {
            conv.Messages.Clear();

            if (Me.Db == null)
            {
                return;
            }

            try
            {
                using var cmd = Me.Db.CreateCommand();
                cmd.CommandText = @"
SELECT f_cli_id, f_seq, f_msg_id, f_from_id, f_msg_type,
       f_content, f_status, f_edit_seq, f_is_revoked, f_created_at
FROM t_chat_message
WHERE f_chat_id = $cid AND f_is_deleted = 0
ORDER BY (f_seq = 0) DESC, f_seq DESC, f_local_id DESC
LIMIT $n";
                cmd.Parameters.AddWithValue("$cid", conv.ChatId);
                cmd.Parameters.AddWithValue("$n", HISTORY_LIMIT);

                using var r = cmd.ExecuteReader();
                while (r.Read())
                {
                    conv.Messages.Add(new ChatMessage
                    {
                        ClientId  = r.GetInt64(0),
                        Seq       = r.GetInt64(1),
                        MsgId     = r.GetInt64(2),
                        FromId    = r.GetInt64(3),
                        Type      = r.GetInt32(4),
                        Content   = r.IsDBNull(5) ? string.Empty : r.GetString(5),
                        Status    = r.GetInt32(6),
                        EditSeq   = r.GetInt64(7),
                        IsRevoked = r.GetInt64(8) != 0,
                        CreatedAt = r.GetInt64(9),
                    });
                }

                conv.Messages.Reverse();
            }
            catch (Exception ex)
            {
                Log.Write($"[ChatTab] 聊天记录加载失败: {ex.Message}");
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

            EmptyState.IsVisible = false;
            ChatView.IsVisible = true;

            // 按 Conversation.Messages 重画(内部会先清屏)
            ChatView.Reload();
        }


        public void AddText(bool mine, string text, string time)
        {
            ChatView.AddText(mine, text, time);
        }


        public void OnPeerMessage(long chatId, SingleChatNtf ntf)
        {
            long fromId = ntf.FromId;

            var msg = new ChatMessage
            {
                ClientId  = (long)ntf.CliId,
                Seq       = ntf.Seq,
                MsgId     = ntf.MsgId,
                FromId    = fromId,
                Type      = (int)ntf.Type,
                Content   = ntf.Content,
                Status    = 1,
                CreatedAt = ntf.CreatedAt,
            };

            if (current != null && current.ChatId == chatId)
            {
                current.Messages.Add(msg);
                ChatView.AddMessage(msg);

                current.LastPreview = msg.Content;
                current.LastTime = msg.CreatedAt;
                ChatPanel.Upsert(current, true);
                UpdateConversation(chatId, msg.Content, msg.CreatedAt, 0);
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

            conv.Messages.Add(msg);
            conv.Unread += 1;
            conv.LastPreview = msg.Content;
            conv.LastTime = msg.CreatedAt;
            ChatPanel.Upsert(conv, true);
            UpdateConversation(chatId, msg.Content, msg.CreatedAt, 1);
        }


        public void OnSelfMessage(SingleChatReq req, long cliId)
        {
            if (current == null)
            {
                return;
            }

            var msg = new ChatMessage
            {
                ClientId  = cliId,
                FromId    = Me.ID,
                Type      = (int)req.Type,
                Content   = req.Content,
                Status    = 0,
                CreatedAt = DateTimeOffset.Now.ToUnixTimeMilliseconds(),
            };

            current.Messages.Add(msg);
            ChatView.AddMessage(msg);

            current.LastPreview = msg.Content;
            current.LastTime = msg.CreatedAt;
            ChatPanel.Upsert(current, true);
            UpdateConversation(current.ChatId, msg.Content, msg.CreatedAt, 0);
        }


        public void OnChatAck(SingleChatRsp rsp)
        {
            long cliId = (long)rsp.CliId;

            foreach (var conv in ChatPanel.Conversations)
            {
                foreach (var m in conv.Messages)
                {
                    if (m.ClientId != cliId || m.FromId != Me.ID)
                    {
                        continue;
                    }

                    if (rsp.Code == 0)
                    {
                        m.Seq       = rsp.Seq;
                        m.MsgId     = rsp.MsgId;
                        m.CreatedAt = rsp.CreatedAt;
                        m.Status    = 1;
                    }
                    else
                    {
                        m.Status = 2;
                    }

                    if (ReferenceEquals(conv, current))
                    {
                        ChatView.UpdateStatus(m);
                    }
                    return;
                }
            }
        }


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
