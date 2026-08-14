using Avalonia;
using Avalonia.Controls;
using CC.Eva;
using CC.Model;
using CC.Proto;
using Lilith.Utils;
using Microsoft.Data.Sqlite;
using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace CC
{
    public partial class ChatTab : BaseTab
    {
        public event Action<SingleChatReq>? SendRequested;

        /// <summary>
        /// 来了一条用户没看到的消息(聊天页不在前面). 宿主据此在导航图标上点红点.
        /// </summary>
        public event Action? Unseen;

        // 当前打开的会话
        private ChatCursor? current;

        // 只加载一次: 页签常驻可视树(靠 IsVisible 切), 进出会重复触发 attach
        private bool loaded;

        public ChatTab()
        {
            InitializeComponent();
            ChatPanel.ChatSelected += OpenChat;
            ChatView.SendRequested += OnSendRequested;
            ChatView.ClearRequested += OnClearRequested;
            ChatView.DeleteRequested += OnDeleteRequested;
        }


        private void OnClearRequested()
        {
            if (current != null)
            {
                ChatProto.Send(new ClearChatReq { PeerId = (uint)current.PeerId },
                               ChatProto.PID_CLEAR_CHAT_REQ);
            }
        }


        private void OnDeleteRequested()
        {
            if (current != null)
            {
                ChatProto.Send(new DeleteChatCursorReq { PeerId = (uint)current.PeerId },
                               ChatProto.PID_DELETE_CHAT_REQ);
            }
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


        private bool windowActive = true;


        /// <summary>
        /// 窗口前台状态变了(MainWindow 调).
        /// </summary>
        public void SetWindowActive(bool active)
        {
            windowActive = active;

            if (active)
            {
                CatchUpRead();
            }
        }


        protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
        {
            base.OnPropertyChanged(change);

            if (change.Property == IsVisibleProperty && IsVisible)
            {
                CatchUpRead();
            }
        }


        private bool Watching()
        {
            return windowActive && IsVisible;
        }


        private void RaiseUnseen()
        {
            if (!IsVisible)
            {
                Unseen?.Invoke();
            }
        }


        private void CatchUpRead()
        {
            if (!Watching() || current == null || current.ReadSeq >= current.RecvSeq)
            {
                return;
            }

            ConfirmRead(current);

            var item = ChatPanel.Find(current.ChatId);
            if (item != null)
            {
                item.Unread = 0;
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
FROM t_chat_cursor ORDER BY f_last_time ASC";

                using var r = cmd.ExecuteReader();
                while (r.Read())
                {
                    var conv = new ChatCursor
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

            localReady = true;

            var pending = pendingCursors;
            pendingCursors = null;
            if (pending != null)
            {
                ApplyCursors(pending);
            }
        }


        private bool localReady;
        private GetChatCursorRsp? pendingCursors;


        /// <summary>
        /// 服务端的会话水位到了(MainWindow 转发): 和本地对账, 算出各会话落下多少条.
        /// </summary>
        public void OnCursorSync(GetChatCursorRsp rsp)
        {
            if (!localReady)
            {
                pendingCursors = rsp;
                return;
            }

            ApplyCursors(rsp);
        }


        private static long PeerOf(long chatId)
        {
            long hi = (long)((ulong)chatId >> 32);
            long lo = (long)(uint)chatId;
            return hi == Me.ID ? lo : hi;
        }


        private void ApplyCursors(GetChatCursorRsp rsp)
        {
            bool unseen = false;

            foreach (var c in rsp.Cursors)
            {
                long chatId = (long)c.ChatId;
                var  conv   = ChatPanel.Find(chatId)?.Conversation;

                if (conv == null)
                {
                    conv = new ChatCursor { ChatId = chatId, PeerId = PeerOf(chatId) };

                    if (orgUsers.TryGetValue(conv.PeerId, out var u))
                    {
                        conv.PeerName   = u.Nickname ?? string.Empty;
                        conv.PeerAvatar = u.Avatar ?? string.Empty;
                    }
                    else
                    {
                        conv.PeerName = $"用户 {conv.PeerId}";
                    }

                    InsertConversation(conv);
                }

                conv.SyncSeq = c.LastSeq;

                if (c.ReadSeq > conv.ReadSeq)
                {
                    conv.ReadSeq = c.ReadSeq;
                }

                long missing = c.LastSeq - conv.RecvSeq;
                if (missing > 0)
                {
                    conv.Unread += (int)missing;
                    conv.LastTime = c.LastTime;
                    unseen = true;
                }

                ChatPanel.Upsert(conv, missing > 0);
                SaveCursor(conv);
            }

            SweepDeleted(rsp);

            if (unseen)
            {
                RaiseUnseen();
            }
        }


        /// <summary>
        /// 历史消息
        /// </summary>
        public void OnChatMessages(GetChatMessageRsp rsp)
        {
            long chatId = (long)rsp.ChatId;
            var  conv   = ChatPanel.Find(chatId)?.Conversation;

            if (conv == null || rsp.Messages.Count == 0)
            {
                return;
            }

            InsertPulled(chatId, rsp);

            long maxSeq = 0;
            foreach (var m in rsp.Messages)
            {
                if (m.Seq > maxSeq)
                {
                    maxSeq = m.Seq;
                }
            }

            if (maxSeq > conv.RecvSeq)
            {
                conv.RecvSeq = maxSeq;
                UpdateRecvSeq(chatId, maxSeq);
            }

            // 服务端是按 seq 倒序给的, 第一条就是最新的
            var newest = rsp.Messages[0];
            if (newest.CreatedAt > conv.LastTime)
            {
                conv.LastPreview = newest.Content;
                conv.LastTime    = newest.CreatedAt;
                UpdateConversation(chatId, conv.LastPreview, conv.LastTime, 0);
            }

            LoadMessages(conv);
            ChatPanel.Upsert(conv);

            if (ReferenceEquals(conv, current))
            {
                ChatView.Reload();

                if (Watching() && current.ReadSeq < current.RecvSeq)
                {
                    ConfirmRead(current);

                    var item = ChatPanel.Find(chatId);
                    if (item != null)
                    {
                        item.Unread = 0;
                    }
                }
            }
        }


        // 整页落库. 一定要包在事务里: 五十条各自提交一次的话, 每次都要落盘同步
        private static void InsertPulled(long chatId, GetChatMessageRsp rsp)
        {
            if (Me.Db == null)
            {
                return;
            }

            try
            {
                using var tx  = Me.Db.BeginTransaction();
                using var cmd = Me.Db.CreateCommand();
                cmd.Transaction = tx;
                cmd.CommandText = @"
INSERT OR IGNORE INTO t_chat_message
(f_chat_id, f_cli_id, f_seq, f_msg_id, f_from_id, f_to_id, f_msg_type,
 f_content, f_status, f_edit_seq, f_edited_at, f_is_revoked, f_created_at)
VALUES ($cid, $cli, $seq, $mid, $from, $to, $type, $content, 1, $esq, $eat, $rev, $ts)";

                var pCli  = cmd.Parameters.Add("$cli", SqliteType.Integer);
                var pSeq  = cmd.Parameters.Add("$seq", SqliteType.Integer);
                var pMid  = cmd.Parameters.Add("$mid", SqliteType.Integer);
                var pFrom = cmd.Parameters.Add("$from", SqliteType.Integer);
                var pTo   = cmd.Parameters.Add("$to", SqliteType.Integer);
                var pType = cmd.Parameters.Add("$type", SqliteType.Integer);
                var pCont = cmd.Parameters.Add("$content", SqliteType.Text);
                var pEsq  = cmd.Parameters.Add("$esq", SqliteType.Integer);
                var pEat  = cmd.Parameters.Add("$eat", SqliteType.Integer);
                var pRev  = cmd.Parameters.Add("$rev", SqliteType.Integer);
                var pTs   = cmd.Parameters.Add("$ts", SqliteType.Integer);
                cmd.Parameters.AddWithValue("$cid", chatId);

                foreach (var m in rsp.Messages)
                {
                    pCli.Value  = (long)m.CliId;
                    pSeq.Value  = m.Seq;
                    pMid.Value  = m.MsgId;
                    pFrom.Value = (long)m.FromId;
                    pTo.Value   = (long)m.ToId;
                    pType.Value = (int)m.MsgType;
                    pCont.Value = m.Content;
                    pEsq.Value  = m.EditSeq;
                    pEat.Value  = m.EditedAt;
                    pRev.Value  = m.IsRevoked ? 1 : 0;
                    pTs.Value   = m.CreatedAt;
                    cmd.ExecuteNonQuery();
                }

                tx.Commit();
            }
            catch (Exception ex)
            {
                Log.Write($"[ChatTab] 历史消息落库失败: {ex.Message}");
            }
        }


        private void SweepDeleted(GetChatCursorRsp rsp)
        {
            var alive = new HashSet<long>();
            foreach (var c in rsp.Cursors)
            {
                alive.Add((long)c.ChatId);
            }

            // 先收集再删: 删除会动 ChatPanel 的子控件, 不能边遍历边改
            var stale = new List<long>();
            foreach (var conv in ChatPanel.Conversations)
            {
                if (conv.RecvSeq > 0 && !alive.Contains(conv.ChatId))
                {
                    stale.Add(conv.ChatId);
                }
            }

            foreach (var chatId in stale)
            {
                OnChatDeleted(chatId);
            }
        }


        // 对账结果落库
        private static void SaveCursor(ChatCursor conv)
        {
            if (Me.Db == null)
            {
                return;
            }

            try
            {
                using var cmd = Me.Db.CreateCommand();
                cmd.CommandText =
                    "UPDATE t_chat_cursor SET f_unread = $u, f_read_seq = $r, f_last_time = $t " +
                    "WHERE f_chat_id = $cid";
                cmd.Parameters.AddWithValue("$u", conv.Unread);
                cmd.Parameters.AddWithValue("$r", conv.ReadSeq);
                cmd.Parameters.AddWithValue("$t", conv.LastTime);
                cmd.Parameters.AddWithValue("$cid", conv.ChatId);
                cmd.ExecuteNonQuery();
            }
            catch (Exception ex)
            {
                Log.Write($"[ChatTab] 会话水位落库失败: {ex.Message}");
            }
        }


        private const int HISTORY_LIMIT = 50;


        private static void LoadMessages(ChatCursor conv)
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

            Int64 chatId = ChatCursor.MakeChatId(Me.ID, peer.ID.Value);

            // 列表里已有就用它挂着的那份(消息都在上面), 没有才新建
            var conv = ChatPanel.Find(chatId)?.Conversation;
            if (conv == null)
            {
                conv = new ChatCursor
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


        // 已读落库
        private static void SaveReadSeq(long chatId)
        {
            if (Me.Db == null)
            {
                return;
            }

            try
            {
                using var cmd = Me.Db.CreateCommand();
                cmd.CommandText =
                    "UPDATE t_chat_cursor SET f_unread = 0, f_read_seq = f_recv_seq WHERE f_chat_id = $cid";
                cmd.Parameters.AddWithValue("$cid", chatId);
                cmd.ExecuteNonQuery();
            }
            catch (Exception ex)
            {
                Log.Write($"[ChatTab] 未读清零失败: {ex.Message}");
            }
        }


        public void OnPeerRead(long chatId, long seq)
        {
            var conv = ChatPanel.Find(chatId)?.Conversation;
            if (conv == null || seq <= conv.PeerReadSeq)
            {
                return;
            }

            conv.PeerReadSeq = seq;
            UpdatePeerReadSeq(chatId, seq);

            if (ReferenceEquals(conv, current))
            {
                ChatView.MarkRead(seq);
            }
        }


        private static void UpdatePeerReadSeq(long chatId, long seq)
        {
            if (Me.Db == null)
            {
                return;
            }

            try
            {
                using var cmd = Me.Db.CreateCommand();
                cmd.CommandText =
                    "UPDATE t_chat_cursor SET f_peer_read_seq = $seq WHERE f_chat_id = $cid AND f_peer_read_seq < $seq";
                cmd.Parameters.AddWithValue("$seq", seq);
                cmd.Parameters.AddWithValue("$cid", chatId);
                cmd.ExecuteNonQuery();
            }
            catch (Exception ex)
            {
                Log.Write($"[ChatTab] 更新对端已读水位失败: {ex.Message}");
            }
        }


        private static void UpdateRecvSeq(long chatId, long seq)
        {
            if (Me.Db == null)
            {
                return;
            }

            try
            {
                using var cmd = Me.Db.CreateCommand();
                cmd.CommandText =
                    "UPDATE t_chat_cursor SET f_recv_seq = $seq WHERE f_chat_id = $cid AND f_recv_seq < $seq";
                cmd.Parameters.AddWithValue("$seq", seq);
                cmd.Parameters.AddWithValue("$cid", chatId);
                cmd.ExecuteNonQuery();
            }
            catch (Exception ex)
            {
                Log.Write($"[ChatTab] 更新已收水位失败: {ex.Message}");
            }
        }


        private static void ConfirmRead(ChatCursor conv)
        {
            conv.Unread  = 0;
            conv.ReadSeq = conv.RecvSeq;
            SaveReadSeq(conv.ChatId);

            ChatProto.Send(new ConfirmChatReq
                           {
                               PeerId = (uint)conv.PeerId,
                               Seq    = conv.RecvSeq,
                           },
                           ChatProto.PID_CONFIRM_CHAT_REQ);
        }


        private static void InsertConversation(ChatCursor conv)
        {
            if (Me.Db == null)
            {
                return;
            }

            try
            {
                using var cmd = Me.Db.CreateCommand();
                cmd.CommandText = "INSERT OR IGNORE INTO t_chat_cursor(f_chat_id, f_peer_id) VALUES ($cid, $pid)";
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

            if (current != null)
            {
                item.Unread = 0;

                if (current.ReadSeq < current.RecvSeq)
                {
                    ConfirmRead(current);
                }
                else if (current.Unread > 0)
                {
                    current.Unread = 0;
                    SaveReadSeq(current.ChatId);
                }
            }

            ChatView.PeerName = item.Nickname;
            ChatView.PeerAvatar = item.Avatar;
            ChatView.PeerStatus = string.Empty;

            EmptyState.IsVisible = false;
            ChatView.IsVisible = true;

            // 按 Conversation.Messages 重画(内部会先清屏)
            ChatView.Reload();

            if (current != null && current.SyncSeq > current.RecvSeq)
            {
                ChatProto.Send(new GetChatMessageReq
                               {
                                   ChatId    = (ulong)current.ChatId,
                                   BeforeSeq = 0,
                                   Limit     = HISTORY_LIMIT,
                               },
                               ChatProto.PID_GET_CHAT_MSG_REQ);
            }
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
                bool watching = Watching();

                current.Messages.Add(msg);
                current.RecvSeq     = msg.Seq;
                current.LastPreview = msg.Content;
                current.LastTime    = msg.CreatedAt;
                ChatView.AddMessage(msg);

                if (!watching)
                {
                    Utils.Sound.Notify();
                    current.Unread += 1;
                    RaiseUnseen();
                }

                ChatPanel.Upsert(current, true);
                UpdateConversation(chatId, msg.Content, msg.CreatedAt, watching ? 0 : 1);

                if (watching)
                {
                    ConfirmRead(current);
                }
                return;
            }

            Utils.Sound.Notify();
            RaiseUnseen();

            var conv = ChatPanel.Find(chatId)?.Conversation;
            if (conv == null)
            {
                conv = new ChatCursor
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
            conv.RecvSeq = msg.Seq;
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

                        if (rsp.Seq > conv.RecvSeq)
                        {
                            conv.RecvSeq = rsp.Seq;
                            UpdateRecvSeq(conv.ChatId, rsp.Seq);
                        }
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


        public void OnChatCleared(long chatId)
        {
            var conv = ChatPanel.Find(chatId)?.Conversation;
            if (conv == null)
            {
                return;
            }

            conv.Messages.Clear();
            conv.LastPreview = string.Empty;
            conv.LastTime = 0;
            conv.Unread = 0;
            ChatPanel.Upsert(conv);

            if (ReferenceEquals(conv, current))
            {
                ChatView.ClearMessages();
            }

            ClearMessagesLocal(chatId);
        }


        public void OnChatDeleted(long chatId)
        {
            var item = ChatPanel.Find(chatId);
            if (item == null)
            {
                return;
            }

            if (ReferenceEquals(item.Conversation, current))
            {
                current = null;
                ChatView.Conversation = null;
                ChatView.ClearMessages();
                ChatView.IsVisible = false;
                EmptyState.IsVisible = true;
            }

            ChatPanel.Remove(item);
            DeleteChatLocal(chatId);
        }


        private static void ClearMessagesLocal(long chatId)
        {
            if (Me.Db == null)
            {
                return;
            }

            try
            {
                using var cmd = Me.Db.CreateCommand();
                cmd.CommandText =
                    "DELETE FROM t_chat_message WHERE f_chat_id = $cid;" +
                    "UPDATE t_chat_cursor SET f_unread = 0, f_last_preview = NULL, f_last_time = 0 " +
                    "WHERE f_chat_id = $cid";
                cmd.Parameters.AddWithValue("$cid", chatId);
                cmd.ExecuteNonQuery();
            }
            catch (Exception ex)
            {
                Log.Write($"[ChatTab] 清空聊天记录失败: {ex.Message}");
            }
        }


        private static void DeleteChatLocal(long chatId)
        {
            if (Me.Db == null)
            {
                return;
            }

            try
            {
                using var cmd = Me.Db.CreateCommand();
                cmd.CommandText =
                    "DELETE FROM t_chat_message WHERE f_chat_id = $cid;" +
                    "DELETE FROM t_chat_cursor WHERE f_chat_id = $cid";
                cmd.Parameters.AddWithValue("$cid", chatId);
                cmd.ExecuteNonQuery();
            }
            catch (Exception ex)
            {
                Log.Write($"[ChatTab] 删除会话失败: {ex.Message}");
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
                    "UPDATE t_chat_cursor SET f_unread = f_unread + $d, f_last_preview = $p, f_last_time = $t " +
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
