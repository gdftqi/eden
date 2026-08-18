using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using System.Threading.Tasks;

namespace CC
{
    public partial class MessageBoxWindow : Window
    {
        // 供 XAML 设计器用; 实际都走下面那个带参数的
        public MessageBoxWindow() : this("提示", string.Empty) { }

        public MessageBoxWindow(string title, string message, string okText = "确定", string cancelText = "取消", bool danger = false)
        {
            InitializeComponent();

            Title = title;
            TitleText.Text = title;
            MessageText.Text = message;
            OkBtn.Content = okText;
            CancelBtn.Content = cancelText;

            // 确认键的配色: 危险操作红色, 其余绿色
            OkBtn.Classes.Add(danger ? "danger" : "primary");

            // cancelText 为空 = 这是个只能"知道了"的通知, 没有第二条路可选
            CancelBtn.IsVisible = !string.IsNullOrEmpty(cancelText);
        }


        /// <summary>
        /// 弹一个模态确认框, 返回用户是否点了确认.
        /// anchor 传调用方自己(任意控件)即可, 宿主窗由它反查.
        /// </summary>
        public static async Task<bool> Confirm(Visual anchor, string title, string message,
                                               string okText = "确定", string cancelText = "取消",
                                               bool danger = false)
        {
            // 模态需要宿主窗.拿不到就当用户取消 -- 这个方法专门用来拦危险操作,
            // 出异常时宁可什么都不做, 也不能默默放行
            if (TopLevel.GetTopLevel(anchor) is not Window owner)
            {
                return false;
            }

            var box = new MessageBoxWindow(title, message, okText, cancelText, danger);

            // 用户直接叉掉窗口(而不是点按钮)时 ShowDialog 返回 default(bool), 也就是 false
            return await box.ShowDialog<bool>(owner);
        }


        /// <summary>
        /// 只有一个确定键的通知框, 不需要宿主窗, 等用户点掉才返回.
        /// 用于主界面已经收起来的场合(如被顶号回登录页) -- 那时没有窗口可以做 ShowDialog 的 owner.
        /// </summary>
        public static Task Alert(string title, string message, string okText = "确定")
        {
            var box = new MessageBoxWindow(title, message, okText, cancelText: string.Empty)
            {
                // 没有 owner 就无从 CenterOwner
                WindowStartupLocation = WindowStartupLocation.CenterScreen,

                // 此刻它可能是全应用唯一的窗口.不进任务栏的话, 一旦被别的程序盖住就再也找不回来
                ShowInTaskbar = true,
            };

            // 没有 owner 就用不了 ShowDialog, 自己把"窗口关闭"接成可 await 的信号.
            // 点确定和直接叉掉都走 Closed, 对通知框来说两者本来就等价
            var done = new TaskCompletionSource();
            box.Closed += (_, _) => done.TrySetResult();

            box.Show();
            box.Activate();
            return done.Task;
        }


        // 无边框窗体没有系统标题栏, 拖动要自己接(与 LoginWindow 同款)
        private void TopBar_PointerPressed(object? sender, PointerPressedEventArgs e)
        {
            if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            {
                BeginMoveDrag(e);
            }
        }

        private void Ok_Click(object? sender, RoutedEventArgs e)
        {
            Close(true);
        }

        private void Cancel_Click(object? sender, RoutedEventArgs e)
        {
            Close(false);
        }
    }
}
