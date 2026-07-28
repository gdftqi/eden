using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace CC
{
    public partial class ContactItem : UserControl
    {// 联系人列表项
        public static readonly StyledProperty<string?> NicknameProperty = AvaloniaProperty.Register<ContactItem, string?>("Nickname");
        public static readonly StyledProperty<string?> SignProperty = AvaloniaProperty.Register<ContactItem, string?>("Sign");
        public static readonly StyledProperty<IImage?> AvatarProperty = AvaloniaProperty.Register<ContactItem, IImage?>("Avatar");
        public static readonly StyledProperty<bool> IsSelectedProperty = AvaloniaProperty.Register<ContactItem, bool>("IsSelected");

        public string? Nickname
        {
            get => GetValue(NicknameProperty);
            set => SetValue(NicknameProperty, value);
        }

        public string? Sign
        {
            get => GetValue(SignProperty);
            set => SetValue(SignProperty, value);
        }

        public IImage? Avatar
        {
            get => GetValue(AvatarProperty);
            set => SetValue(AvatarProperty, value);
        }

        public bool IsSelected
        {
            get => GetValue(IsSelectedProperty);
            set => SetValue(IsSelectedProperty, value);
        }

        public ContactItem()
        {
            InitializeComponent();
        }

        protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
        {
            base.OnPropertyChanged(change);
            if (change.Property == IsSelectedProperty)
            {
                Body.Classes.Set("selected", IsSelected);
            }
        }
    }
}
