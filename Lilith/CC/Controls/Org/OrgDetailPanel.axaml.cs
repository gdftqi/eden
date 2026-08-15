using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Platform.Storage;
using CC.Eva;
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


        /// <summary>
        /// 灌入选人抽屉的候选名单.抽屉自己不拉接口, 由 OrgTab 拿 /get_org 那一份统一分发.
        /// </summary>
        public void SetCandidates(IEnumerable<User> users)
        {
            Picker.SetSource(users);
        }


        public void Show(OrgGroup item)
        {
            dept = item;

            // 进页面时抽屉应当是关的, 编辑框也不该是开着的: 上次离开时可能正开着
            Picker.HideNow();
            EndEdit(TitleView, TitleEditBar);
            EndEdit(RemarkView, RemarkEditBar);
            DropPendingAvatar();

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

            // 有待确认的新头像时别去动它, 否则刚选的图会被服务端那版顶掉
            if (pendingAvatar == null)
            {
                ApplyAvatar(dept.Dept?.Avatar);
            }

            Picker.SetChecked(MemberIDs(dept));
        }


        private void ApplyRemark(string? remark)
        {
            if (string.IsNullOrWhiteSpace(remark))
            {
                RemarkText.Text = "添加部门描述";
                RemarkText.Foreground = new SolidColorBrush(Color.Parse("#008069"));
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
            var target = dept;
            if (target?.Dept?.ID == null || user.ID == null)
            {
                return;
            }

            var d = target.Dept;

            bool ok = await MessageBoxWindow.Confirm(
                this,
                "移除成员",
                $"确定把 {user.Nickname} 移出 {d.Name} 吗?",
                okText: "移除",
                danger: true);

            if (!ok)
            {
                return;
            }

            try
            {
                await UpdateDepart.POST(d.ID.Value, delUserIDs: new List<Int64> { user.ID.Value });
            }
            catch (Exception ex)
            {
                Tips.Error($"移除失败: {ex.Message}");
                return;
            }

            MemberToggled?.Invoke(target, user, false);
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


        // 选好但还没确认的新头像.图和文件各留一份: 图用来预览, 文件等点勾时才真上传
        private Bitmap? pendingAvatar;
        private IStorageFile? pendingAvatarFile;


        private async void PickAvatar_PointerPressed(object? sender, PointerPressedEventArgs e)
        {
            if (dept?.Dept?.ID == null)
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

            // 到此为止只是贴上来看看.真正上传要等用户点勾
            pendingAvatar = preview;
            pendingAvatarFile = file;

            ShowAvatar(preview);
            AvatarEditBar.IsVisible = true;
        }


        private async void AvatarOk_Click(object? sender, RoutedEventArgs e)
        {
            // 上传和保存都要 await, 这期间用户可能已经切到别的部门去了 --
            // 先把目标抓在手里, 别等回来再读 dept
            var group = dept;
            var d = group?.Dept;
            var file = pendingAvatarFile;
            var preview = pendingAvatar;

            if (group == null || d?.ID == null || file == null || preview == null)
            {
                DropPendingAvatar();
                return;
            }

            // 请求飞出去之后就不该再点第二次
            AvatarEditBar.IsVisible = false;

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
                DropPendingAvatar();
                return;
            }

            // 顺手放进缓存: 刚上传的图已经在手里, 不必等切走再回来时重新下一遍
            Avatars.Put(url, preview);

            try
            {
                await UpdateDepart.POST(d.ID.Value, avatar: url);
            }
            catch (Exception ex)
            {
                Tips.Error($"头像保存失败: {ex.Message}");
                DropPendingAvatar();
                return;
            }

            pendingAvatar = null;
            pendingAvatarFile = null;

            d.Avatar = url;
            Tips.Success("头像已更新");
            Changed?.Invoke(group);
        }


        private void AvatarCancel_Click(object? sender, RoutedEventArgs e)
        {
            DropPendingAvatar();
        }


        // 丢掉待确认的那张, 界面退回服务端存着的那一版
        private void DropPendingAvatar()
        {
            pendingAvatar = null;
            pendingAvatarFile = null;
            AvatarEditBar.IsVisible = false;

            ApplyAvatar(dept?.Dept?.Avatar);
        }


        // ---------------- 就地编辑 ----------------

        // Enter 提交会先隐藏输入框, 隐藏又会触发 LostFocus 再提交一次.
        // 用它挡住第二次
        private bool committing;


        private void RenameDept_Click(object? sender, RoutedEventArgs e)
        {
            BeginEdit(TitleView, TitleEditBar, TitleEdit, dept?.Dept?.Name);
        }


        private void EditRemark_Click(object? sender, RoutedEventArgs e)
        {
            BeginEdit(RemarkView, RemarkEditBar, RemarkEdit, dept?.Dept?.Desc);
        }


        // 勾/叉 上的 Tag 标明它管的是哪个输入框
        private void EditOk_Click(object? sender, RoutedEventArgs e)
        {
            Commit((sender as Control)?.Tag as string == "title" ? TitleEdit : RemarkEdit, save: true);
        }


        private void EditCancel_Click(object? sender, RoutedEventArgs e)
        {
            Commit((sender as Control)?.Tag as string == "title" ? TitleEdit : RemarkEdit, save: false);
        }


        // 收放的是整条 bar(输入框 + 勾 + 叉), 不是单个输入框
        private void BeginEdit(Control view, Control bar, TextBox edit, string? text)
        {
            if (dept == null)
            {
                return;
            }

            edit.Text = text ?? string.Empty;
            view.IsVisible = false;
            bar.IsVisible = true;

            edit.Focus();
            edit.SelectAll();
        }


        private void EndEdit(Control view, Control bar)
        {
            bar.IsVisible = false;
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


        // 点到别处 = 放弃这次编辑.
        // 既然勾/叉就摆在旁边, 就不该再留"失焦也算提交"这条看不见的路 --
        // 手滑点一下就发一个请求出去, 太容易改错
        private void TitleEdit_LostFocus(object? sender, RoutedEventArgs e)
        {
            Commit(TitleEdit, save: false);
        }


        private void RemarkEdit_LostFocus(object? sender, RoutedEventArgs e)
        {
            Commit(RemarkEdit, save: false);
        }


        private async void Commit(TextBox edit, bool save)
        {
            bool isTitle = ReferenceEquals(edit, TitleEdit);
            var bar = isTitle ? (Control)TitleEditBar : RemarkEditBar;

            if (committing || !bar.IsVisible)
            {
                return;
            }

            // 无论存不存, 编辑框都先收起来 -- 取消这条路不该被数据状态挡住.
            // 收起来会把焦点弹走, 顺带回弹一次 LostFocus, committing 就是用来挡那一下的;
            // 挡完立刻松开: 请求还飞在路上时不该把别处的编辑也一并挡掉,
            // 本框自己的重入有上面的 !bar.IsVisible 拦着
            committing = true;
            EndEdit(isTitle ? TitleView : RemarkView, bar);
            committing = false;

            var group = dept;
            var d = group?.Dept;
            if (!save || group == null || d?.ID == null)
            {
                return;
            }

            var text = (edit.Text ?? string.Empty).Trim();

            if (isTitle)
            {
                // 名字是必填, 清空当作没改
                if (text.Length == 0 || text == d.Name)
                {
                    return;
                }
            }
            else if (text == (d.Desc ?? string.Empty))
            {
                return;
            }

            // 先落服务端再改本地: 存不下来本地就原样不动, 省掉回滚这一步
            try
            {
                await UpdateDepart.POST(d.ID.Value,
                                        name: isTitle ? text : null,
                                        desc: isTitle ? null : text);
            }
            catch (Exception ex)
            {
                Tips.Error(ex.Message);
                return;
            }

            if (isTitle)
            {
                d.Name = text;
            }
            else
            {
                d.Desc = text;
            }

            // 请求期间用户可能已经切去看别的部门了, 那就别拿这份数据去刷面板
            if (ReferenceEquals(dept, group))
            {
                Refresh();
            }

            Changed?.Invoke(group);
        }

  
        private void AddMember_Click(object? sender, RoutedEventArgs e)
        {
            if (dept != null) Picker.Toggle();
        }

        private void AddMemberRow_PointerPressed(object? sender, PointerPressedEventArgs e)
        {
            if (dept != null) Picker.Toggle();
        }

        private async void OnPickerToggled(User user, bool on)
        {
            var target = dept;
            if (target?.Dept?.ID == null || user.ID == null)
            {
                return;
            }

            var ids = new List<Int64> { user.ID.Value };

            try
            {
                await UpdateDepart.POST(target.Dept.ID.Value,
                                        addUserIDs: on ? ids : null,
                                        delUserIDs: on ? null : ids);
            }
            catch (Exception ex)
            {
                Tips.Error(ex.Message);

                // 抽屉里的勾已经先翻过去了, 服务端这边没落成就得翻回来
                Picker.SetChecked(MemberIDs(target));
                return;
            }

            MemberToggled?.Invoke(target, user, on);
        }


        private static IEnumerable<Int64> MemberIDs(OrgGroup group)
        {
            return group.Members.Where(u => u.ID.HasValue).Select(u => u.ID!.Value);
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
