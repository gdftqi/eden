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
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;

namespace CC
{
    public partial class OrgEditPanel : UserControl
    {
        public event Action? Cancelled;

        private readonly HashSet<string> selected = new();

        public OrgEditPanel()
        {
            InitializeComponent();
            Picker.Toggled += OnPickerToggled;
            RefreshMembers();
        }


        public void Reset()
        {
            NameBox.Text = string.Empty;
            DescBox.Text = string.Empty;
            ClearAvatar();

            selected.Clear();
            Picker.SetChecked(selected);
            Picker.HideNow();

            RefreshMembers();
            NameBox.Focus();
        }


        // ---------------- 选人抽屉 ----------------

        // 选中的头像文件.发请求时要先上传拿到 url
        private string? avatarPath;


        private async void PickAvatar_PointerPressed(object? sender, PointerPressedEventArgs e)
        {
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

            try
            {
                await using var stream = await files[0].OpenReadAsync();
                AvatarImage.Source = new Bitmap(stream);
            }
            catch (Exception ex)
            {
                Tips.Error($"图片打不开: {ex.Message}");
                return;
            }

            AvatarImage.IsVisible = true;
            AvatarIcon.IsVisible = false;
            
            avatarPath = files[0].TryGetLocalPath();
            
            Debug.WriteLine($"{avatarPath}");
        }


        private void ClearAvatar()
        {
            avatarPath = null;
            AvatarImage.Source = null;
            AvatarImage.IsVisible = false;
            AvatarIcon.IsVisible = true;
        }


        private void TogglePicker_PointerPressed(object? sender, PointerPressedEventArgs e)
        {
            Picker.Toggle();
        }


        private void OnPickerToggled(string nickname, bool on)
        {
            if (on)
            {
                selected.Add(nickname);
            }
            else
            {
                selected.Remove(nickname);
            }

            RefreshMembers();
        }


        private void RemoveMember(string nickname)
        {
            selected.Remove(nickname);
            Picker.SetChecked(selected);
            RefreshMembers();
        }


        private void RefreshMembers()
        {
            MemberList.Children.Clear();
            foreach (var nick in selected)
            {
                MemberList.Children.Add(BuildMemberRow(nick));
            }

            MemberHeader.Text = selected.Count > 0 ? $"{selected.Count} 位成员" : "尚未选择成员";
        }


        private Control BuildMemberRow(string nickname)
        {
            var sign = ContactSource.All.FirstOrDefault(c => c.Nickname == nickname)?.Sign ?? string.Empty;

            var avatar = new Border
            {
                Width = 42,
                Height = 42,
                CornerRadius = new CornerRadius(21),
                ClipToBounds = true,
                Margin = new Thickness(0, 0, 14, 0),
                Child = new Image { Source = ContactSource.Avatar, Stretch = Stretch.UniformToFill },
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

            var remove = new Avalonia.Controls.Shapes.Path
            {
                Width = 12,
                Height = 12,
                Stroke = new SolidColorBrush(Color.Parse("#9AA0A6")),
                StrokeThickness = 1.6,
                StrokeLineCap = PenLineCap.Round,
                Data = Geometry.Parse("M1,1 L11,11 M11,1 L1,11"),
                VerticalAlignment = VerticalAlignment.Center,
            };

            var grid = new Grid { ColumnDefinitions = new ColumnDefinitions("Auto,*,Auto") };
            Grid.SetColumn(avatar, 0);
            Grid.SetColumn(texts, 1);
            Grid.SetColumn(remove, 2);
            grid.Children.Add(avatar);
            grid.Children.Add(texts);
            grid.Children.Add(remove);

            var row = new Border { Child = grid };
            row.Classes.Add("row");
            ToolTip.SetTip(row, "点击移除");
            row.PointerPressed += (_, _) => RemoveMember(nickname);
            return row;
        }


        private async void Save_Click(object? sender, RoutedEventArgs e)
        {
            var name = (NameBox.Text ?? string.Empty).Trim();
            if (name.Length == 0)
            {
                Tips.Error("请填写部门名称");
                return;
            }

            // 描述选填, 不做校验; Trim 后为空就是"没填"
            var desc = (DescBox.Text ?? string.Empty).Trim();

            // TODO 获取 UserList
            try
            {
                await CreateDepart.POST(name, "TODO", desc, null);
            }
            catch (Exception ex)
            {
                Tips.Error(ex.Message);
                return;
            }

            Tips.Success("添加部门成功");

            BaseTab.Notify<OrgTab>(new Message(MsgID.DeptCreated,
                new Department { Name = name, Desc = desc }, this));

            Reset();
        }


        private void Cancel_Click(object? sender, RoutedEventArgs e)
        {
            Cancelled?.Invoke();
        }
    }
}
