using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media.Imaging;
using Avalonia.Platform.Storage;
using CC.Eva;
using CC.Model;
using Lilith.Core;
using System;
using System.Linq;
using System.Threading.Tasks;
using Upload = Lilith.Core.Eva.Upload;

namespace CC
{
    // 个人信息: 改自己的昵称和头像.
    // 校验, 发请求, 结果提示都在本控件里闭环.
    public partial class SettingInfo : UserControl
    {
        // 当前登录用户. 进页面时拉一次
        private User? me;

        // 这次新选的头像文件. 存文件对象而不是路径 --
        // TryGetLocalPath() 对云盘/虚拟位置会返回 null
        private IStorageFile? avatarFile;

        public SettingInfo()
        {
            InitializeComponent();
        }


        /// <summary>
        /// 拉一次自己的资料并填进表单. 每次进这一页都调.
        /// </summary>
        public async Task Reload()
        {
            avatarFile = null;

            GetOrg.Response rsp;
            try
            {
                // 暂时没有"查我自己"的接口, 先从组织架构里按 id 捞.
                // 等 Eva 出了 /get_user 换掉这里
                rsp = await GetOrg.POST();
            }
            catch (Exception ex)
            {
                Tips.Error($"加载个人信息失败: {ex.Message}");
                return;
            }

            me = rsp.Users?.FirstOrDefault(u => u.ID == HttpSession.Instance.UserID);
            if (me == null)
            {
                Tips.Error("找不到当前用户");
                return;
            }

            NickBox.Text = me.Nickname ?? string.Empty;
            UserNameText.Text = me.Username ?? string.Empty;
            Avatars.Bind(AvatarImage, me.Avatar);
        }


        private async void PickAvatar_PointerPressed(object? sender, PointerPressedEventArgs e)
        {
            var top = TopLevel.GetTopLevel(this);
            if (top == null)
            {
                return;
            }

            var files = await top.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
            {
                Title = "选择头像",
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

            // 只是本地预览, 真正上传在点保存时 -- 选了又改主意的话不该留下垃圾对象
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

            avatarFile = files[0];
        }


        private async void Save_Click(object? sender, RoutedEventArgs e)
        {
            if (me == null)
            {
                Tips.Error("个人信息还没加载好");
                return;
            }

            var nick = (NickBox.Text ?? string.Empty).Trim();
            if (nick.Length == 0 || nick.Length > 16)
            {
                Tips.Error("请填写昵称");
                return;
            }

            // 头像先传, 失败就整个中止 -- 昵称改了头像没改会让人以为都没生效
            string? avatar = null;
            if (avatarFile != null)
            {
                try
                {
                    var props = await avatarFile.GetBasicPropertiesAsync();
                    await using var stream = await avatarFile.OpenReadAsync();
                    avatar = await Upload.POST(stream, avatarFile.Name, (long)(props.Size ?? 0));
                }
                catch (Exception ex)
                {
                    Tips.Error($"头像上传失败: {ex.Message}");
                    return;
                }
            }

            // 没改的项传 null, 服务端就不动它
            string? nickArg = nick == (me.Nickname ?? string.Empty) ? null : nick;
            if (nickArg == null && avatar == null)
            {
                Tips.Success("没有改动");
                return;
            }

            User user;
            try
            {
                user = await UpdateUser.Profile(nickArg, avatar);
            }
            catch (Exception ex)
            {
                Tips.Error(ex.Message);
                return;
            }

            me = user;
            avatarFile = null;

            // 刚传的图已经在手里, 顺手进缓存, 别等下次显示时重新下
            if (avatar != null && AvatarImage.Source is Bitmap bmp)
            {
                Avatars.Put(avatar, bmp);
            }

            Tips.Success("保存成功");
        }
    }
}
