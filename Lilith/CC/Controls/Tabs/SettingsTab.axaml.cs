using Avalonia.Controls;

namespace CC
{
    public partial class SettingsTab : BaseTab
    {
        public SettingsTab()
        {
            InitializeComponent();

            ListPanel.Selected += OnSelected;
            SecurityView.PasswordChanged += OnPasswordChanged;
        }


        private void OnSelected(SettingsListPanel.Item item)
        {
            switch (item)
            {
                case SettingsListPanel.Item.Info:
                    // 每次进来都重填: 上次可能选了图没保存就切走了
                    InfoView.Reload();
                    ShowOnly(InfoView);
                    break;

                case SettingsListPanel.Item.Security:
                    SecurityView.Reset();
                    ShowOnly(SecurityView);
                    break;
            }
        }


        // 改完密码服务端已经把会话作废了
        private async void OnPasswordChanged()
        {
            await MessageBoxWindow.Alert("密码已修改", "请用新密码重新登录", "重新登录");

            if (TopLevel.GetTopLevel(this) is MainWindow main)
            {
                main.BackToLogin();
            }
        }


        private void ShowOnly(Control target)
        {
            InfoView.IsVisible = target == InfoView;
            SecurityView.IsVisible = target == SecurityView;
            EmptyState.IsVisible = target == EmptyState;
        }
    }
}
