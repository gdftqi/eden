using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using CC.Model;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace CC
{
    public partial class OrgItem : UserControl
    {
        public static readonly StyledProperty<Department?> DeptProperty = AvaloniaProperty.Register<OrgItem, Department?>("Dept");
        public static readonly StyledProperty<User?> UserProperty = AvaloniaProperty.Register<OrgItem, User?>("User");
        public static readonly StyledProperty<string?> TitleProperty = AvaloniaProperty.Register<OrgItem, string?>("Title");
        public static readonly StyledProperty<string?> LastMessageProperty = AvaloniaProperty.Register<OrgItem, string?>("LastMessage");
        public static readonly StyledProperty<string?> TimeProperty = AvaloniaProperty.Register<OrgItem, string?>("Time");
        public static readonly StyledProperty<int> UnreadProperty = AvaloniaProperty.Register<OrgItem, int>("Unread");
        public static readonly StyledProperty<bool> IsSelectedProperty = AvaloniaProperty.Register<OrgItem, bool>("IsSelected");

        public Department? Dept
        {
            get => GetValue(DeptProperty);
            set => SetValue(DeptProperty, value);
        }

        public User? User
        {
            get => GetValue(UserProperty);
            set => SetValue(UserProperty, value);
        }

        public string? Title
        {
            get => GetValue(TitleProperty);
            set => SetValue(TitleProperty, value);
        }

        public string DeptName => Dept?.Name ?? string.Empty;

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


        public IList<User> Members { get; } = new List<User>();

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
            else if (change.Property == DeptProperty || change.Property == UserProperty)
            {
                ApplyKind();
            }
        }


        private void ApplyKind()
        {
            bool isDept = Dept != null;

            // 群聊行标上 (群): 它和下面的成员行长得一样, 不标看不出这行点开是群聊
            Title = isDept ? $"{Dept!.Name} (群)" : User?.Nickname;

            var tip = isDept ? Dept!.Desc : User?.Username;
            ToolTip.SetTip(Body, string.IsNullOrWhiteSpace(tip) ? null : tip);

            ApplyAvatar(isDept ? Dept!.Avatar : User?.Avatar, isDept);
        }


        private void ApplyAvatar(string? url, bool isDept)
        {
            // 成员没设头像就用默认照片; 部门没设就显示楼宇图标
            var fallback = isDept ? null : Avatars.Default;

            // 已经缓存的先同步铺上, 避免滚动时先闪一下占位再变成图
            var bmp = Avatars.Cached(url);
            Show(bmp ?? fallback);

            if (bmp != null || string.IsNullOrEmpty(url))
            {
                return;
            }

            // 下载期间这一行可能已经被 Rebuild 掉或者换了绑定对象,
            // 回来时要确认还是同一张图才敢往上贴
            var want = url;
            _ = Avatars.Load(url).ContinueWith(t =>
            {
                if (t.Result != null && CurrentAvatarUrl() == want)
                {
                    Show(t.Result);
                }
            }, TaskScheduler.FromCurrentSynchronizationContext());
        }


        private string? CurrentAvatarUrl()
        {
            return Dept != null ? Dept.Avatar : User?.Avatar;
        }


        private void Show(IImage? image)
        {
            PhotoImage.Source = image;
            PhotoAvatar.IsVisible = image != null;
            IconAvatar.IsVisible = image == null;
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
