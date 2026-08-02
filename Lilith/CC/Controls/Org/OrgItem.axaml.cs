using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using System.Collections.Generic;

namespace CC
{
    // 部门列表项 -- 一个部门就是一个群聊, 所以这里的字段与 ChatItem 一一对应,
    // 只是"昵称"叫 DeptName,头像固定为楼宇图标.
    public partial class OrgItem : UserControl
    {
        public static readonly StyledProperty<string?> DeptNameProperty = AvaloniaProperty.Register<OrgItem, string?>("DeptName");
        public static readonly StyledProperty<string?> RemarkProperty = AvaloniaProperty.Register<OrgItem, string?>("Remark");
        public static readonly StyledProperty<string?> LastMessageProperty = AvaloniaProperty.Register<OrgItem, string?>("LastMessage");
        public static readonly StyledProperty<string?> TimeProperty = AvaloniaProperty.Register<OrgItem, string?>("Time");
        public static readonly StyledProperty<int> UnreadProperty = AvaloniaProperty.Register<OrgItem, int>("Unread");
        public static readonly StyledProperty<bool> IsSelectedProperty = AvaloniaProperty.Register<OrgItem, bool>("IsSelected");

        public string? DeptName
        {
            get => GetValue(DeptNameProperty);
            set => SetValue(DeptNameProperty, value);
        }

        /// <summary>
        /// 部门备注.列表行里放不下, 悬停时以 ToolTip 呈现; 为空则不挂 ToolTip.
        /// </summary>
        public string? Remark
        {
            get => GetValue(RemarkProperty);
            set => SetValue(RemarkProperty, value);
        }

        public string? LastMessage
        {
            get => GetValue(LastMessageProperty);
            set => SetValue(LastMessageProperty, value);
        }

        public string? Time
        {
            get => GetValue(TimeProperty);
            set => SetValue(TimeProperty, value);
        }

        public int Unread
        {
            get => GetValue(UnreadProperty);
            set => SetValue(UnreadProperty, value);
        }

        public bool IsSelected
        {
            get => GetValue(IsSelectedProperty);
            set => SetValue(IsSelectedProperty, value);
        }

        /// <summary>
        /// 部门成员(暂用昵称, 接服务端后换 uid).
        /// 不做 StyledProperty -- 没有 XAML 绑定它, 集合型属性走样式系统也没意义.
        /// </summary>
        public IList<string> Members { get; } = new List<string>();

        public OrgItem()
        {
            InitializeComponent();
            ApplyUnread();
        }

        protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
        {
            base.OnPropertyChanged(change);
            if (change.Property == UnreadProperty)
            {
                ApplyUnread();
            }
            else if (change.Property == IsSelectedProperty)
            {
                Body.Classes.Set("selected", IsSelected);
            }
            else if (change.Property == RemarkProperty)
            {
                // 置 null 而不是空串: 空串也会弹出一个空气泡
                ToolTip.SetTip(Body, string.IsNullOrWhiteSpace(Remark) ? null : Remark);
            }
        }

        private void ApplyUnread()
        {
            if (Badge == null)
            {
                return;
            }

            Badge.IsVisible = Unread > 0;
            BadgeText.Text = Unread > 99 ? "99+" : Unread.ToString();
        }

        protected override void OnPointerReleased(PointerReleasedEventArgs e)
        {
            base.OnPointerReleased(e);
            Unread = 0;
        }
    }
}
