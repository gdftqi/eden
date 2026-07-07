using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace CC
{
    public partial class EmployeeItem : UserControl
    {// 员工列表项(信息展示同 ContactItem)
        public static readonly StyledProperty<string?> NicknameProperty = AvaloniaProperty.Register<EmployeeItem, string?>("Nickname");
        public static readonly StyledProperty<string?> SignProperty = AvaloniaProperty.Register<EmployeeItem, string?>("Sign");
        public static readonly StyledProperty<IImage?> AvatarProperty = AvaloniaProperty.Register<EmployeeItem, IImage?>("Avatar");
        public static readonly StyledProperty<bool> IsSelectedProperty = AvaloniaProperty.Register<EmployeeItem, bool>("IsSelected");

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

        public EmployeeItem()
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
