using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using System;

namespace CC
{
    // 设置页左侧列表.
    // 与 OrgListPanel 同样的分工: 只管选中和高亮, 右侧显示什么由宿主决定.
    public partial class SettingsListPanel : UserControl
    {
        public enum Item { Info, Security }

        public event Action<Item>? Selected;

        public SettingsListPanel()
        {
            InitializeComponent();
        }


        /// <summary>
        /// 选中某一项: 旧的熄灭, 新的点亮, 再通知宿主.
        /// 公开出去是为了让宿主也能选中(比如进页面时默认停在"个人信息").
        /// </summary>
        public void Select(Item item)
        {
            InfoRow.Classes.Set("selected", item == Item.Info);
            SecurityRow.Classes.Set("selected", item == Item.Security);

            Selected?.Invoke(item);
        }


        private void InfoRow_PointerPressed(object? sender, PointerPressedEventArgs e)
        {
            Select(Item.Info);
        }


        private void SecurityRow_PointerPressed(object? sender, PointerPressedEventArgs e)
        {
            Select(Item.Security);
        }


        // 设置项就两条, 搜索只做显示过滤, 不必抽成通用的列表模型
        private void SearchBox_TextChanged(object? sender, TextChangedEventArgs e)
        {
            var kw = (SearchBox.Text ?? string.Empty).Trim();

            InfoRow.IsVisible = kw.Length == 0 || "个人信息".Contains(kw, StringComparison.OrdinalIgnoreCase);
            SecurityRow.IsVisible = kw.Length == 0 || "隐私安全".Contains(kw, StringComparison.OrdinalIgnoreCase);
        }


        private void ClearSearch_Click(object? sender, RoutedEventArgs e)
        {
            SearchBox.Text = string.Empty;
            SearchBox.Focus();
        }
    }
}
