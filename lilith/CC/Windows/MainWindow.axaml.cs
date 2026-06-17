using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;

namespace CC
{
    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            InitializeComponent();
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
            // TODO: 打开设置面板
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
            SetNav(ChatIcon);
            // TODO: 切换到聊天
        }

        private void Contacts_Click(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
        {
            SetNav(ContactsIcon);
            // TODO: 切换到联系人列表
        }

        private void Org_Click(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
        {
            SetNav(OrgIcon);
            // TODO: 切换到组织架构
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
    }
}