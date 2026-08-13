using Avalonia;
using Avalonia.Controls;
using CC.Eva;
using CC.Model;
using Lilith.Utils;
using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace CC
{
    public partial class ChatTab : BaseTab
    {
        public event Action<string>? SendRequested;

        // 当前打开的会话
        private ChatConversation? current;

        // 只加载一次: 页签常驻可视树(靠 IsVisible 切), 进出会重复触发 attach
        private bool loaded;

        public ChatTab()
        {
            InitializeComponent();
            ChatPanel.ChatSelected += OpenChat;
            ChatView.SendRequested += t => SendRequested?.Invoke(t);
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


        /// <summary>
        /// 从本地库加载会话列表. 名字/头像不落会话表, 从组织架构解析.
        /// </summary>
        private async Task Reload()
        {
            if (Me.Db == null)
            {
                return;
            }

            var users = new Dictionary<Int64, User>();
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
    }
}
