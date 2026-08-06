using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform.Storage;
using CC.Model;
using Lilith.Core.Eva;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace CC
{
    public partial class OrgDetailPanel : UserControl
    {
        public event Action? BackRequested;
        public event Action<OrgGroup>? Changed;
        public event Action<OrgGroup, User, bool>? MemberToggled;
        public event Action<OrgGroup>? SearchRequested;

        // 当前展示的部门
        private OrgGroup? dept;

        public OrgDetailPanel()
        {
            InitializeComponent();
            Picker.Toggled += OnPickerToggled;
        }


        public void Show(OrgGroup item)
        {
            dept = item;

            // 进页面时抽屉应当是关的, 编辑框也不该是开着的: 上次离开时可能正开着
            Picker.HideNow();
            EndEdit(TitleView, TitleEdit);
            EndEdit(RemarkView, RemarkEdit);

            Refresh();
        }


        public void Refresh()
        {
            if (dept == null)
            {
                return;
            }

            DeptTitle.Text = dept.Dept?.Name ?? string.Empty;
            MemberSummary.Text = $"{dept.Members.Count} 位成员";
            MemberHeader.Text = $"{dept.Members.Count} 位成员";

            ApplyRemark(dept.Dept?.Desc);
            BuildMembers(dept.Members);

            ApplyAvatar(dept.Dept?.Avatar);

            Picker.SetChecked(dept.Members.Select(u => u.Nickname ?? string.Empty));
        }


        private void ApplyRemark(string? remark)
        {
            if (string.IsNullOrWhiteSpace(remark))
            {
                RemarkText.Text = "添加部门描述";
                RemarkText.Foreground = new SolidColorBrush(Color.Parse("#43A047"));
            }
            else
            {
                RemarkText.Text = remark;
                RemarkText.Foreground = new SolidColorBrush(Color.Parse("#1E1E1E"));
            }
        }


        // ---------------- 成员行 ----------------

        private void BuildMembers(IEnumerable<User> members)
        {
            MemberList.Children.Clear();
            foreach (var u in members)
            {
                MemberList.Children.Add(BuildMemberRow(u));
            }
        }


        private Control BuildMemberRow(User user)
        {
            var nickname = user.Nickname ?? string.Empty;

            var sign = user.Username ?? string.Empty;

            var photo = new Image { Stretch = Stretch.UniformToFill };
            Avatars.Bind(photo, user.Avatar);

            var avatar = new Border
            {
                Width = 42,
                Height = 42,
                CornerRadius = new CornerRadius(21),
                ClipToBounds = true,
                Margin = new Thickness(0, 0, 14, 0),
                Child = photo,
            };

            var texts = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Spacing = 2 };
            texts.Children.Add(new TextBlock
            {
                Text = nickname,
                FontSize = 14,
                Foreground = new SolidColorBrush(Color.Parse("#1E1E1E")),
                TextTrimming = TextTrimming.CharacterEllipsis,
            });

            if (sign.Length > 0)
            {
                texts.Children.Add(new TextBlock
                {
                    Text = sign,
                    FontSize = 12,
                    Foreground = new SolidColorBrush(Color.Parse("#8A9099")),
                    TextTrimming = TextTrimming.CharacterEllipsis,
                });
            }

            var remove = new Button
            {
                VerticalAlignment = VerticalAlignment.Center,
                Content = new Avalonia.Controls.Shapes.Rectangle
                {
                    Width = 12,
                    Height = 2,
                    RadiusX = 1,
                    RadiusY = 1,
                    Fill = Brushes.White,
                },
            };
            remove.Classes.Add("removeBtn");
            ToolTip.SetTip(remove, "移出本部门");
            remove.Click += (_, _) => ConfirmRemove(user);

            var grid = new Grid
            {
                ColumnDefinitions = new ColumnDefinitions("Auto,*,Auto"),
            };
            Grid.SetColumn(avatar, 0);
            Grid.SetColumn(texts, 1);
            Grid.SetColumn(remove, 2);
            grid.Children.Add(avatar);
            grid.Children.Add(texts);
            grid.Children.Add(remove);

            var row = new Border { Child = grid };
            row.Classes.Add("row");
            return row;
        }


        // 移人是不可撤销的动作, 先问一句再抛给宿主
        private async void ConfirmRemove(User user)
        {
            if (dept == null)
            {
                return;
            }

            var target = dept;

            bool ok = await MessageBoxWindow.Confirm(
                this,
                "移除成员",
                $"确定把 {user.Nickname} 移出 {target.Dept?.Name} 吗?",
                okText: "移除",
                danger: true);

            if (ok)
            {
                MemberToggled?.Invoke(target, user, false);
            }
        }


        // ---------------- 动作 ----------------

        private void Back_Click(object? sender, RoutedEventArgs e)
        {
            BackRequested?.Invoke();
        }

        // ---------------- 头像 ----------------

        // 服务端给的是 url, 缓存里没有就异步下. 回来时确认当前还是这个部门才敢往上贴 --
        // 下载期间用户可能已经切走了
        private void ApplyAvatar(string? url)
        {
            var bmp = Avatars.Cached(url);
            ShowAvatar(bmp);

            if (bmp != null || string.IsNullOrEmpty(url))
            {
                return;
            }

            var want = url;
            _ = Avatars.Load(url).ContinueWith(t =>
            {
                if (t.Result != null && dept?.Dept?.Avatar == want)
                {
                    ShowAvatar(t.Result);
                }
            }, TaskScheduler.FromCurrentSynchronizationContext());
        }


        private void ShowAvatar(IImage? image)
        {
            AvatarImage.Source = image;
            AvatarImage.IsVisible = image != null;
            AvatarIcon.IsVisible = image == null;
        }


        private async void PickAvatar_PointerPressed(object? sender, PointerPressedEventArgs e)
        {
            if (dept?.Dept == null)
            {
                return;
            }

            var top = TopLevel.GetTopLevel(this);
            if (top == null)
            {
                return;
            }

            var files = await top.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
            {
                Title = "选择部门头像",
                AllowMultiple = false,
                FileTypeFilter = new[]
                {
                    new FilePickerFileType("图片")
                    {
                        Patterns  = new[] { "*.png", "*.jpg", "*.jpeg" },
                        MimeTypes = new[] { "image/png", "image/jpeg" },
                    },
                },
            });

            if (files.Count == 0)
            {
                return;
            }

            var file = files[0];

            Bitmap preview;
            try
            {
                await using var s = await file.OpenReadAsync();
                preview = new Bitmap(s);
            }
            catch (Exception ex)
            {
                Tips.Error($"图片打不开: {ex.Message}");
                return;
            }

            ShowAvatar(preview);

            string url;
            try
            {
                var props = await file.GetBasicPropertiesAsync();
                await using var stream = await file.OpenReadAsync();
                url = await Upload.POST(stream, file.Name, (long)(props.Size ?? 0));
            }
            catch (Exception ex)
            {
                Tips.Error($"头像上传失败: {ex.Message}");
                return;
            }

            dept.Dept.Avatar = url;

            // 顺手放进缓存: 刚上传的图已经在手里, 不必等切走再回来时重新下一遍
            Avatars.Put(url, preview);

            // TODO: 服务端还没有改部门的接口, 这里只改了本地
            Tips.Success("头像已更新");
            Changed?.Invoke(dept);
        }


        // ---------------- 就地编辑 ----------------

        // Enter 提交会先隐藏输入框, 隐藏又会触发 LostFocus 再提交一次.
        // 用它挡住第二次
        private bool committing;


        private void RenameDept_Click(object? sender, RoutedEventArgs e)
        {
            BeginEdit(TitleView, TitleEdit, dept?.Dept?.Name);
        }


        private void EditRemark_Click(object? sender, RoutedEventArgs e)
        {
            BeginEdit(RemarkView, RemarkEdit, dept?.Dept?.Desc);
        }


        private void BeginEdit(Control view, TextBox edit, string? text)
        {
            if (dept == null)
            {
                return;
            }

            edit.Text = text ?? string.Empty;
            view.IsVisible = false;
            edit.IsVisible = true;

            edit.Focus();
            edit.SelectAll();
        }


        private void EndEdit(Control view, TextBox edit)
        {
            edit.IsVisible = false;
            view.IsVisible = true;
        }


        private void Edit_KeyDown(object? sender, KeyEventArgs e)
        {
            if (sender is not TextBox edit)
            {
                return;
            }

            if (e.Key == Key.Enter)
            {
                e.Handled = true;
                Commit(edit, save: true);
            }
            else if (e.Key == Key.Escape)
            {
                e.Handled = true;
                Commit(edit, save: false);
            }
        }


        private void TitleEdit_LostFocus(object? sender, RoutedEventArgs e)
        {
            Commit(TitleEdit, save: true);
        }


        private void RemarkEdit_LostFocus(object? sender, RoutedEventArgs e)
        {
            Commit(RemarkEdit, save: true);
        }


        private void Commit(TextBox edit, bool save)
        {
            if (committing || !edit.IsVisible || dept?.Dept == null)
            {
                return;
            }

            committing = true;
            try
            {
                bool isTitle = ReferenceEquals(edit, TitleEdit);
                EndEdit(isTitle ? TitleView : RemarkView, edit);

                if (!save)
                {
                    return;
                }

                var text = (edit.Text ?? string.Empty).Trim();

                if (isTitle)
                {
                    // 名字是必填, 清空当作没改
                    if (text.Length == 0 || text == dept.Dept.Name)
                    {
                        return;
                    }
                    dept.Dept.Name = text;
                }
                else
                {
                    if (text == (dept.Dept.Desc ?? string.Empty))
                    {
                        return;
                    }
                    dept.Dept.Desc = text;
                }

                // TODO: 服务端还没有改部门的接口, 先只改本地.
                // 接上之后改成: 发请求 -> 成功再落地, 失败 Tips.Error 并把旧值填回去
                Refresh();
                Changed?.Invoke(dept);
            }
            finally
            {
                committing = false;
            }
        }

  
        private void AddMember_Click(object? sender, RoutedEventArgs e)
        {
            if (dept != null) Picker.Toggle();
        }

        private void AddMemberRow_PointerPressed(object? sender, PointerPressedEventArgs e)
        {
            if (dept != null) Picker.Toggle();
        }

        private void OnPickerToggled(string nickname, bool on)
        {
            if (dept == null)
            {
                return;
            }

 
            var user = dept.Members.FirstOrDefault(u => u.Nickname == nickname)
                       ?? new User { Nickname = nickname };

            MemberToggled?.Invoke(dept, user, on);
        }


        private void Mute_Click(object? sender, RoutedEventArgs e)
        {
            bool muted = MuteState.Text == "始终静音";
            MuteState.Text = muted ? "接收全部消息" : "始终静音";
        }

        private void Search_Click(object? sender, RoutedEventArgs e)
        {
            if (dept != null) SearchRequested?.Invoke(dept);
        }
    }
}
