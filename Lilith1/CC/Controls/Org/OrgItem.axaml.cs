using Avalonia;
using Avalonia.Controls;

namespace CC
{
    public partial class OrgItem : UserControl
    {// 部门列表项: 部门图标 + 部门名 + 展开箭头
        public static readonly StyledProperty<string?> DeptNameProperty = AvaloniaProperty.Register<OrgItem, string?>("DeptName");
        public static readonly StyledProperty<bool> IsExpandedProperty = AvaloniaProperty.Register<OrgItem, bool>("IsExpanded");

        public string? DeptName
        {
            get => GetValue(DeptNameProperty);
            set => SetValue(DeptNameProperty, value);
        }

        public bool IsExpanded
        {
            get => GetValue(IsExpandedProperty);
            set => SetValue(IsExpandedProperty, value);
        }

        public OrgItem()
        {
            InitializeComponent();
        }

        protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
        {
            base.OnPropertyChanged(change);
            if (change.Property == IsExpandedProperty)
            {
                Body.Classes.Set("expanded", IsExpanded);
            }
        }
    }
}
