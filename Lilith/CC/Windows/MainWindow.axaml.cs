using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using CC.Model;
using CC.Proto;
using Lilith.Components;
using Lilith.Core;
using Lilith.Utils;
using System;
using System.Threading.Tasks;

namespace CC
{
    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            InitializeComponent();
            RefreshAvatar();
            TabChat.SendRequested += OnSendText;
            // 主窗接管 Hydra 的高层回调(pump 在 App 里已接好, 这里不用管)
            Hydra.Instance.SetOnStateChanged(OnHydraState).SetOnPackage(OnPackage);
        }


        /// <summary>
        /// 按当前登录用户重刷左下角头像.在设置页改完头像后也要调一次.
        /// </summary>
        public void RefreshAvatar()
        {
            Avatars.Bind(Avatar, Me.User?.Avatar);
        }


        // 切换页签: 叠放的四个 Tab 只显示一个
        private void ShowTab(Control tab)
        {
            TabChat.IsVisible = tab == TabChat;
            TabContact.IsVisible = tab == TabContact;
            TabOrg.IsVisible = tab == TabOrg;
            TabSettings.IsVisible = tab == TabSettings;
        }

        private void Button_Click(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
        {
            if (Application.Current?.ApplicationLifetime is IControlledApplicationLifetime lifetime)
            {
                lifetime.Shutdown();
            }
        }

        private void Minimize_Click(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
        {
            WindowState = WindowState.Minimized;
        }

        private void Settings_Click(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
        {
            SetNav(SettingsIcon);
            ShowTab(TabSettings);
        }

        private static readonly Avalonia.Media.IBrush ActiveIcon = new Avalonia.Media.SolidColorBrush(Avalonia.Media.Color.Parse("#66BB6A"));
        private static readonly Avalonia.Media.IBrush InactiveIcon = Avalonia.Media.Brushes.White;

        private void SetNav(Avalonia.Controls.Shapes.Path active)
        {
            ChatIcon.Fill = active == ChatIcon ? ActiveIcon : InactiveIcon;
            ContactsIcon.Fill = active == ContactsIcon ? ActiveIcon : InactiveIcon;
            OrgIcon.Fill = active == OrgIcon ? ActiveIcon : InactiveIcon;
            SettingsIcon.Fill = active == SettingsIcon ? ActiveIcon : InactiveIcon;
        }

        private void Chat_Click(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
        {
            OpenChatTab();
        }


        /// <summary>
        /// 切到聊天页.
        /// </summary>
        public void OpenChatTab()
        {
            SetNav(ChatIcon);
            ShowTab(TabChat);
        }

        private void Contacts_Click(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
        {
            SetNav(ContactsIcon);
            ShowTab(TabContact);
        }

        private void Org_Click(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
        {
            SetNav(OrgIcon);
            ShowTab(TabOrg);
        }

        private void ToggleMaximize()
        {
            bool toMax = WindowState != WindowState.Maximized;
            WindowState = toMax ? WindowState.Maximized : WindowState.Normal;
            MaxIcon.Data = Avalonia.Media.Geometry.Parse(toMax ? "M3,7 H11 V14 H3 Z M6,7 V4 H14 V12 H11" : "M1,1 H15 V15 H1 Z");
        }

        private void Maximize_Click(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
        {
            ToggleMaximize();
        }

        private void TopBar_PointerPressed(object? sender, Avalonia.Input.PointerPressedEventArgs e)
        {
            if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            {
                if (e.ClickCount == 2)
                {
                    ToggleMaximize();
                }
                else
                {
                    this.BeginMoveDrag(e);
                }
            }
        }

        private void Resize_PointerPressed(object? sender, Avalonia.Input.PointerPressedEventArgs e)
        {
            if (sender is Control c && c.Tag is string edge && e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            {
                this.BeginResizeDrag(System.Enum.Parse<WindowEdge>(edge), e);
            }
        }

        // Hydra 状态变化(主线程回调): 驱动浮窗 / 掉线回登录
        private void OnHydraState(HydraState prev, HydraState cur)
        {
            switch (cur)
            {
                case HydraState.Reconnecting:
                    ShowConnState(ConnState.Reconnecting);
                    break;

                case HydraState.Connected:
                    // 只在"重连成功"时弹绿点; 首次登录连上不弹(登录流程已处理)
                    if (prev == HydraState.Reconnecting)
                    {
                        ShowConnState(ConnState.Connected);
                    }
                    break;

                case HydraState.Disconnected:
                    // 重连放弃 / 会话失效 -> 回登录页
                    BackToLogin();
                    break;
            }
        }


        private bool leaving;

        // 公开出去是为了让页签也能触发
        public async void BackToLogin()
        {
            if (leaving)
            {
                return;
            }
            leaving = true;

            // 会话已由 Hydra 关闭
            uint code = Hydra.Instance.KickCode;
            if (code != 0)
            {
                Hide();

                await MessageBoxWindow.Alert("已下线", Package.ErrorText(code), "重新登录");
            }

            // 原因已经用提示框说过了, 不必再在登录页重复一遍
            var login = new LoginWindow();
            if (Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            {
                desktop.MainWindow = login;
            }
            login.Show();
            Close();
        }

        // ============ 连接状态提示条 ============

        public enum ConnState { Disconnected, Reconnecting, Connected }

        private static readonly Avalonia.Media.IBrush DotRed = new Avalonia.Media.SolidColorBrush(Avalonia.Media.Color.Parse("#E53935"));
        private static readonly Avalonia.Media.IBrush DotGreen = new Avalonia.Media.SolidColorBrush(Avalonia.Media.Color.Parse("#43A047"));
        private int connStateGen;   // 状态代次: Connected 的 1.5s 自动收起用它防"期间又切了别的状态时误收"

        // 显示提示条并滑出
        public void ShowConnState(ConnState state)
        {
            int gen = ++connStateGen;

            switch (state)
            {
                case ConnState.Disconnected:
                    ConnText.Text = "连接断开";
                    ConnDot.Fill = DotRed;
                    ConnDot.IsVisible = true;
                    ConnSpinner.IsVisible = false;
                    break;

                case ConnState.Reconnecting:
                    ConnText.Text = "重新连接";
                    ConnDot.IsVisible = false;
                    ConnSpinner.IsVisible = true;
                    break;

                case ConnState.Connected:
                    ConnText.Text = "连接成功";
                    ConnDot.Fill = DotGreen;
                    ConnDot.IsVisible = true;
                    ConnSpinner.IsVisible = false;
                    break;
            }

            ConnBanner.RenderTransform = Avalonia.Media.Transformation.TransformOperations.Parse("translateX(0px)");

            if (state == ConnState.Connected)
            {
                AutoHideConnBanner(gen);   // 连接成功: 停留 1.5s 后自动收起
            }
        }


        public void HideConnBanner()
        {
            ConnBanner.RenderTransform = Avalonia.Media.Transformation.TransformOperations.Parse("translateX(-180px)");
        }


        private async void AutoHideConnBanner(int gen)
        {
            await Task.Delay(1500);
            if (gen == connStateGen)
            {
                HideConnBanner();
            }
        }

        // ============ 操作结果提示(Toast) ============

        private static readonly Avalonia.Media.Geometry ToastOkGlyph =
            Avalonia.Media.Geometry.Parse("M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z");
        private static readonly Avalonia.Media.Geometry ToastFailGlyph =
            Avalonia.Media.Geometry.Parse("M11 7h2v6h-2zm0 8h2v2h-2z");

        private static readonly Avalonia.Media.IBrush ToastOkBg = new Avalonia.Media.SolidColorBrush(Avalonia.Media.Color.Parse("#43A047"));
        private static readonly Avalonia.Media.IBrush ToastFailBg = new Avalonia.Media.SolidColorBrush(Avalonia.Media.Color.Parse("#E53935"));

        // 与 connStateGen 同样的用意: 连着来两条时, 后一条不该被前一条的定时器收掉
        private int toastGen;

        /// <summary>
        /// 顶部弹一条一闪即收的提示.用于"刚才那步成没成"这类事后反馈,
        /// 不打断操作 -- 需要用户点一下才继续的场合请用 MessageBoxWindow.
        /// </summary>
        public void ShowToast(string text, bool ok = true)
        {
            int gen = ++toastGen;

            ToastText.Text = text;
            ToastIcon.Background = ok ? ToastOkBg : ToastFailBg;
            ToastGlyph.Data = ok ? ToastOkGlyph : ToastFailGlyph;

            ToastBanner.Opacity = 1;
            ToastBanner.RenderTransform = Avalonia.Media.Transformation.TransformOperations.Parse("translateY(0px)");

            AutoHideToast(gen);
        }


        private async void AutoHideToast(int gen)
        {
            await Task.Delay(2200);

            // 期间又来了新的一条: 收起交给那一条负责, 这里撒手
            if (gen != toastGen)
            {
                return;
            }

            ToastBanner.Opacity = 0;
            ToastBanner.RenderTransform = Avalonia.Media.Transformation.TransformOperations.Parse("translateY(-60px)");
        }

        // Hydra 主线程回调: 收包按 PID 分发. 包在回调返回后即还池, 不可留引用
        private void OnPackage(Package pkg)
        {
            switch (pkg.PID)
            {
                case Package.PID_TER_ERROR:
                    if (Package.DecodeError(pkg, out uint code, out uint dstId, out uint pid))
                    {
                        Log.Write($"[CC] 请求未送达: code = {code}, dst_id = {dstId}, pid = {pid}");
                        ShowToast(Package.ErrorText(code), false);
                    }
                    return;

                case Package.PID_TER_ENT_RSP:
                    // 后端登记回执: code=0 即已建档, 可收它的推送
                    if (Package.DecodeEnterRsp(pkg, out uint entUid, out uint entCode))
                    {
                        Log.Write($"[CC] 后端登记回执: serv = 0x{pkg.SrcID:X8}, uid = {entUid}, code = {entCode}");
                    }
                    return;

                case ChatProto.PID_SINGLE_CHAT_RSP:
                    OnChatRsp(pkg);
                    return;

                case ChatProto.PID_SINGLE_CHAT_NTF:
                    OnChatNtf(pkg);
                    return;

                case ChatProto.PID_CLEAR_CHAT_RSP:
                case ChatProto.PID_CLEAR_CHAT_NTF:
                    OnClearChat(pkg);
                    return;

                case ChatProto.PID_DELETE_CHAT_RSP:
                case ChatProto.PID_DELETE_CHAT_NTF:
                    OnDeleteChat(pkg);
                    return;

                default:
                    Log.Write($"[CC] 未处理的 PID: {pkg.PID}, src = 0x{pkg.SrcID:X8}");
                    return;
            }
        }


        // 单聊 ACK: 按 cli_id(=f_local_id) 认领, 回填服务端三件套并置成功;
        // code != 0 只标失败, 不删行 -- 内容还在, 用户可看着原文重发
        private void OnChatRsp(Package pkg)
        {
            SingleChatRsp rsp;
            try
            {
                rsp = SingleChatRsp.Parser.ParseFrom(pkg.Payload, 0, pkg.PayloadLength);
            }
            catch (Exception ex)
            {
                Log.Write($"[CC] SINGLE_CHAT_RSP 解析失败: {ex.Message}");
                return;
            }

            if (Me.Db == null)
            {
                return;
            }

            try
            {
                using var cmd = Me.Db.CreateCommand();
                if (rsp.Code == 0)
                {
                    cmd.CommandText =
                        "UPDATE t_chat_message SET f_seq = $seq, f_msg_id = $mid, f_created_at = $ts, f_status = 1 " +
                        "WHERE f_local_id = $cli AND f_from_id = $me AND f_seq = 0";
                    cmd.Parameters.AddWithValue("$seq", rsp.Seq);
                    cmd.Parameters.AddWithValue("$mid", rsp.MsgId);
                    cmd.Parameters.AddWithValue("$ts", rsp.CreatedAt);
                }
                else
                {
                    Log.Write($"[CC] 消息被服务端拒绝: cli_id = {rsp.CliId}, code = {rsp.Code}");
                    ShowToast("消息发送失败", false);
                    cmd.CommandText =
                        "UPDATE t_chat_message SET f_status = 2 WHERE f_local_id = $cli AND f_from_id = $me AND f_seq = 0";
                }
                cmd.Parameters.AddWithValue("$cli", (long)rsp.CliId);
                cmd.Parameters.AddWithValue("$me", Me.ID);
                cmd.ExecuteNonQuery();
            }
            catch (Exception ex)
            {
                Log.Write($"[CC] ACK 回填失败: {ex.Message}");
            }

            TabChat.OnChatAck(rsp);
        }


        private void OnClearChat(Package pkg)
        {
            try
            {
                long chatId;
                if (pkg.PID == ChatProto.PID_CLEAR_CHAT_RSP)
                {
                    var rsp = ClearChatRsp.Parser.ParseFrom(pkg.Payload, 0, pkg.PayloadLength);
                    if (rsp.Code != 0)
                    {
                        Log.Write($"[CC] 清空聊天失败: code = {rsp.Code}");
                        ShowToast("清空失败, 请稍后重试", false);
                        return;
                    }
                    chatId = (long)rsp.ChatId;
                }
                else
                {
                    chatId = (long)ClearChatNtf.Parser.ParseFrom(pkg.Payload, 0, pkg.PayloadLength).ChatId;
                }

                TabChat.OnChatCleared(chatId);
            }
            catch (Exception ex)
            {
                Log.Write($"[CC] CLEAR_CHAT 解析失败: {ex.Message}");
            }
        }


        private void OnDeleteChat(Package pkg)
        {
            try
            {
                long chatId;
                if (pkg.PID == ChatProto.PID_DELETE_CHAT_RSP)
                {
                    var rsp = DeleteChatCursorRsp.Parser.ParseFrom(pkg.Payload, 0, pkg.PayloadLength);
                    if (rsp.Code != 0)
                    {
                        Log.Write($"[CC] 删除会话失败: code = {rsp.Code}");
                        ShowToast("删除失败, 请稍后重试", false);
                        return;
                    }
                    chatId = (long)rsp.ChatId;
                }
                else
                {
                    chatId = (long)DeleteChatCursorNtf.Parser.ParseFrom(pkg.Payload, 0, pkg.PayloadLength).ChatId;
                }

                TabChat.OnChatDeleted(chatId);
            }
            catch (Exception ex)
            {
                Log.Write($"[CC] DELETE_CHAT 解析失败: {ex.Message}");
            }
        }


        private void OnChatNtf(Package pkg)
        {
            SingleChatNtf ntf;
            try
            {
                ntf = SingleChatNtf.Parser.ParseFrom(pkg.Payload, 0, pkg.PayloadLength);
            }
            catch (Exception ex)
            {
                Log.Write($"[CC] SINGLE_CHAT_NTF 解析失败: {ex.Message}");
                return;
            }

            long chatId = Model.ChatConversation.MakeChatId(Me.ID, ntf.FromId);

            if (Me.Db != null)
            {
                try
                {
                    using var cmd = Me.Db.CreateCommand();
                    cmd.CommandText =
                        "INSERT OR IGNORE INTO t_chat_conversation(f_chat_id, f_peer_id) VALUES ($cid, $peer);" +
                        "INSERT OR IGNORE INTO t_chat_message(f_chat_id, f_cli_id, f_seq, f_msg_id, f_from_id, f_to_id, f_msg_type, f_content, f_status, f_created_at) " +
                        "VALUES ($cid, $cli, $seq, $mid, $peer, $me, $type, $content, 1, $ts)";
                    cmd.Parameters.AddWithValue("$cid", chatId);
                    cmd.Parameters.AddWithValue("$peer", (long)ntf.FromId);
                    cmd.Parameters.AddWithValue("$cli", (long)ntf.CliId);
                    cmd.Parameters.AddWithValue("$seq", ntf.Seq);
                    cmd.Parameters.AddWithValue("$mid", ntf.MsgId);
                    cmd.Parameters.AddWithValue("$me", Me.ID);
                    cmd.Parameters.AddWithValue("$type", (int)ntf.Type);
                    cmd.Parameters.AddWithValue("$content", ntf.Content);
                    cmd.Parameters.AddWithValue("$ts", ntf.CreatedAt);
                    cmd.ExecuteNonQuery();
                }
                catch (Exception ex)
                {
                    Log.Write($"[CC] 推送落库失败: {ex.Message}");
                }
            }

            TabChat.OnPeerMessage(chatId, ntf);
        }


        private void OnSendText(SingleChatReq req)
        {
            long cliId = InsertPendingMessage(req);
            if (cliId <= 0)
            {
                ShowToast("发送失败", false);
                return;
            }

            req.CliId = (ulong)cliId;
            TabChat.OnSelfMessage(req, cliId);
            ChatProto.Send(req, ChatProto.PID_SINGLE_CHAT_REQ);
        }


        private static long InsertPendingMessage(SingleChatReq req)
        {
            if (Me.Db == null)
            {
                return 0;
            }

            try
            {
                using var cmd = Me.Db.CreateCommand();
                cmd.CommandText =
                    "INSERT INTO t_chat_message(f_chat_id, f_cli_id, f_from_id, f_to_id, f_msg_type, f_content, f_created_at) " +
                    "VALUES ($cid, 0, $from, $to, $type, $content, $ts);" +
                    "UPDATE t_chat_message SET f_cli_id = f_local_id WHERE f_local_id = last_insert_rowid();" +
                    "SELECT last_insert_rowid();";
                cmd.Parameters.AddWithValue("$cid", Model.ChatConversation.MakeChatId(Me.ID, req.ToId));
                cmd.Parameters.AddWithValue("$from", Me.ID);
                cmd.Parameters.AddWithValue("$to", req.ToId);
                cmd.Parameters.AddWithValue("$type", (int)req.Type);
                cmd.Parameters.AddWithValue("$content", req.Content);
                cmd.Parameters.AddWithValue("$ts", DateTimeOffset.Now.ToUnixTimeMilliseconds());
                return (long)(cmd.ExecuteScalar() ?? 0L);
            }
            catch (Exception ex)
            {
                Log.Write($"[CC] 消息落库失败: {ex.Message}");
                return 0;
            }
        }
    }
}
