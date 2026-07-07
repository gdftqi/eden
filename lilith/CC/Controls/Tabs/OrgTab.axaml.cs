using Avalonia.Controls;

namespace CC
{
    // 组织架构页: OrgListPanel + 右侧员工详情(后期填)
    public partial class OrgTab : UserControl
    {
        public OrgTab()
        {
            InitializeComponent();
            OrgPanel.EmployeeSelected += OpenEmployee;
            OrgPanel.AddOrgRequested += OnAddOrg;
        }

        private void OpenEmployee(EmployeeItem item)
        {
            // TODO: 右侧显示员工详情
        }

        private void OnAddOrg()
        {
            // TODO: 添加部门界面
        }
    }
}
