using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media;
using CC.Eva;
using CC.Model;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Threading.Tasks;

namespace CC
{
    /// <summary>
    /// WhatsApp 风格「新聊天」面板: 新建群组 / 添加联系人 / 联系人列表.
    /// </summary>
    public partial class NewChatPanel : UserControl
    {
        public event Action? Closed;
        public event Action? NewGroupRequested;
        public event Action? AddContactRequested;
        public event Action<User>? ContactPicked;

        private readonly List<User> allUsers = new();
        private string filter = "";

        public NewChatPanel()
        {
            InitializeComponent();
        }


        /// <summary>
        /// 打开面板并刷新联系人.
        /// </summary>
        public void Show()
        {
            IsVisible = true;
            SearchBox.Text = string.Empty;
            filter = "";
            _ = Reload();
        }


        public void Hide()
        {
            IsVisible = false;
            Closed?.Invoke();
        }


        private async Task Reload()
        {
            GetOrg.Response rsp;
            try
            {
                rsp = await GetOrg.POST();
            }
            catch (Exception ex)
            {
                Tips.Error($"加载联系人失败: {ex.Message}");
                return;
            }

            allUsers.Clear();
            foreach (var u in rsp.Users ?? new List<User>())
            {
                if (u.ID != null)
                {
                    allUsers.Add(u);
                }
            }

            RebuildList();
        }


        private void RebuildList()
        {
            ContactList.Children.Clear();

            var q = filter.Trim();
            IEnumerable<User> src = allUsers;
            if (q.Length > 0)
            {
                src = allUsers.Where(u =>
                    (u.Nickname?.Contains(q, StringComparison.OrdinalIgnoreCase) ?? false)
                    || (u.Username?.Contains(q, StringComparison.OrdinalIgnoreCase) ?? false)
                    || (u.PhoneNum?.Contains(q, StringComparison.OrdinalIgnoreCase) ?? false));
            }

            // 自己置顶
            var me = Me.User;
            var ordered = src
                .OrderBy(u => me?.ID != null && u.ID == me.ID ? 0 : 1)
                .ThenBy(u => u.Nickname ?? u.Username ?? "", StringComparer.CurrentCultureIgnoreCase)
                .ToList();

            string? lastLetter = null;
            foreach (var u in ordered)
            {
                var letter = LetterOf(u);
                if (letter != lastLetter)
                {
                    lastLetter = letter;
                    ContactList.Children.Add(new TextBlock
                    {
                        Text = letter,
                        FontSize = 13,
                        FontWeight = FontWeight.SemiBold,
                        Foreground = new SolidColorBrush(Color.Parse("#008069")),
                        Margin = new Avalonia.Thickness(16, 10, 16, 4),
                    });
                }

                ContactList.Children.Add(BuildRow(u));
            }
        }


        private static string LetterOf(User u)
        {
            var name = (u.Nickname ?? u.Username ?? "?").Trim();
            if (name.Length == 0)
            {
                return "#";
            }

            var ch = char.ToUpper(name[0], CultureInfo.CurrentCulture);
            if (ch >= 'A' && ch <= 'Z')
            {
                return ch.ToString();
            }

            return name[0].ToString();
        }


        private Control BuildRow(User user)
        {
            var isSelf = Me.User?.ID != null && user.ID == Me.User.ID;
            var title = isSelf
                ? $"{user.Nickname ?? user.Username}（自己）"
                : (user.Nickname ?? user.Username ?? "");
            var sub = isSelf ? "给自己发消息" : (user.Username ?? "");

            var avatar = new Image
            {
                Stretch = Avalonia.Media.Stretch.UniformToFill,
                Source = Avatars.Cached(user.Avatar) ?? Avatars.Default,
            };
            if (Avatars.Cached(user.Avatar) == null && !string.IsNullOrEmpty(user.Avatar))
            {
                _ = Avatars.Load(user.Avatar).ContinueWith(t =>
                {
                    if (t.Result != null)
                    {
                        avatar.Source = t.Result;
                    }
                }, TaskScheduler.FromCurrentSynchronizationContext());
            }

            var avatarBox = new Border
            {
                Width = 44,
                Height = 44,
                CornerRadius = new Avalonia.CornerRadius(22),
                ClipToBounds = true,
                Margin = new Avalonia.Thickness(0, 0, 14, 0),
                Child = avatar,
            };
            Grid.SetColumn(avatarBox, 0);

            var texts = new StackPanel
            {
                VerticalAlignment = Avalonia.Layout.VerticalAlignment.Center,
                Spacing = 2,
            };
            texts.Children.Add(new TextBlock
            {
                Text = title,
                FontSize = 14.5,
                FontWeight = FontWeight.SemiBold,
                Foreground = new SolidColorBrush(Color.Parse("#111B21")),
                TextTrimming = TextTrimming.CharacterEllipsis,
            });
            if (!string.IsNullOrEmpty(sub))
            {
                texts.Children.Add(new TextBlock
                {
                    Text = sub,
                    FontSize = 12.5,
                    Foreground = new SolidColorBrush(Color.Parse("#667781")),
                    TextTrimming = TextTrimming.CharacterEllipsis,
                });
            }
            Grid.SetColumn(texts, 1);

            var grid = new Grid();
            grid.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Auto));
            grid.ColumnDefinitions.Add(new ColumnDefinition(GridLength.Star));
            grid.Children.Add(avatarBox);
            grid.Children.Add(texts);

            var row = new Border { Classes = { "contact" }, Child = grid };
            row.PointerPressed += (_, _) => ContactPicked?.Invoke(user);
            return row;
        }


        private void Back_Click(object? sender, RoutedEventArgs e) => Hide();

        private void ClearSearch_Click(object? sender, RoutedEventArgs e)
        {
            SearchBox.Text = string.Empty;
            SearchBox.Focus();
        }

        private void SearchBox_TextChanged(object? sender, TextChangedEventArgs e)
        {
            filter = SearchBox.Text ?? "";
            RebuildList();
        }

        private void NewGroup_PointerPressed(object? sender, PointerPressedEventArgs e)
        {
            NewGroupRequested?.Invoke();
        }

        private void AddContact_PointerPressed(object? sender, PointerPressedEventArgs e)
        {
            AddContactRequested?.Invoke();
        }
    }
}
