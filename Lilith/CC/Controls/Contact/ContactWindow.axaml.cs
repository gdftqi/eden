using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Media;
using System;

namespace CC
{
    // 联系人详情页: SetContact 填数据; 点左上角 X 抛 CloseRequested 给宿主
    public partial class ContactWindow : UserControl
    {
        public event Action? CloseRequested;

        // 点了"消息": 宿主负责切到聊天页并打开/创建与该联系人的会话
        public event Action? ChatRequested;

        public ContactWindow()
        {
            InitializeComponent();
        }

        // 填充联系人信息: 头像 / 昵称 / 手机号(可多个) / 状态签名
        public void SetContact(IImage avatar, string nick, string[] phones, string status = "")
        {
            FaceImg.Source = avatar;
            NickText.Text = nick;
            StatusText.Text = status;
            BlockText.Text = $"封锁{nick}";
            ReportText.Text = $"举报{nick}";

            PhoneList.Children.Clear();
            foreach (var phone in phones)
            {
                PhoneList.Children.Add(new TextBlock
                {
                    Text = phone,
                    FontSize = 15,
                    Foreground = new SolidColorBrush(Color.Parse("#667085")),
                    HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Center,
                });
            }
        }

        private void Close_Click(object? sender, RoutedEventArgs e)
        {
            CloseRequested?.Invoke();
        }

        private void Chat_Click(object? sender, RoutedEventArgs e)
        {
            ChatRequested?.Invoke();
        }
    }
}
