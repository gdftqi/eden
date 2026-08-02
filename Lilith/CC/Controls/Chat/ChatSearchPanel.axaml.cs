using Avalonia.Controls;
using Avalonia.Interactivity;
using System;

namespace CC
{
    // 搜索消息.对群聊和单聊通用 -- 它只认一个"在哪里搜"的名字, 不关心对方是人还是部门.
    public partial class ChatSearchPanel : UserControl
    {
        public event Action? BackRequested;

        // 搜的是哪个会话(只用于提示文案)
        private string target = string.Empty;

        public ChatSearchPanel()
        {
            InitializeComponent();
        }


        /// <summary>
        /// 打开搜索.每次都清空上次的关键词 -- 换了会话还留着旧词会误导.
        /// </summary>
        public void Show(string targetName)
        {
            target = targetName ?? string.Empty;
            SearchBox.Text = string.Empty;
            ApplyState();
            SearchBox.Focus();
        }


        private void SearchBox_TextChanged(object? sender, TextChangedEventArgs e)
        {
            ApplyState();
        }


        private void ApplyState()
        {
            var q = (SearchBox.Text ?? string.Empty).Trim();

            if (q.Length == 0)
            {
                HintText.Text = $"在{target}中搜索消息。";
                HintText.IsVisible = true;
                ResultScroll.IsVisible = false;
                return;
            }

            // TODO: 真正的检索要等消息落库(Model/ChatMeta 那套).
            // 在那之前如实显示"没找到", 不做假结果.
            ResultList.Children.Clear();
            HintText.Text = "没有找到相关消息";
            HintText.IsVisible = true;
            ResultScroll.IsVisible = false;
        }


        private void ClearSearch_Click(object? sender, RoutedEventArgs e)
        {
            SearchBox.Text = string.Empty;
            SearchBox.Focus();
        }


        private void ByDate_Click(object? sender, RoutedEventArgs e)
        {
            // TODO: 按日期跳转, 同样等消息落库
        }


        private void Back_Click(object? sender, RoutedEventArgs e)
        {
            BackRequested?.Invoke();
        }
    }
}
