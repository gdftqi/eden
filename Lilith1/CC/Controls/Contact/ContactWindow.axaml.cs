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

            LoadSampleMedia(avatar);
        }

        // 临时: 媒体条塞几张占位缩略图(以后换成真实的影音/链接/文档)
        private void LoadSampleMedia(IImage thumb)
        {
            const int count = 5;
            MediaStrip.Children.Clear();
            for (int i = 0; i < count; i++)
            {
                MediaStrip.Children.Add(new Border
                {
                    Width = 96, Height = 96, CornerRadius = new CornerRadius(10), ClipToBounds = true,
                    Child = new Image { Source = thumb, Stretch = Stretch.UniformToFill },
                });
            }
            MediaCount.Text = count.ToString();
        }

        private void Close_Click(object? sender, RoutedEventArgs e)
        {
            CloseRequested?.Invoke();
        }
    }
}
