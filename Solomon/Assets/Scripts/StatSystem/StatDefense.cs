using System;

namespace Solomon
{
    [Serializable]
    public class StatDefense
    {
        /// <summary>
        /// 物理护甲. 不是固定减免, 而是走 伤害 * 伤害/(伤害+护甲) 这个形状 --
        /// 护甲值可以无限堆而不会出现完全免疫, 收益自动递减.
        /// 结算前先被攻击方的 ArmorReduction 扣掉一部分.
        /// </summary>
        public Stat Armor;

        /// <summary>
        /// 闪避率, 取值 0~1. 0.15 表示 15% 的几率完全免疫这一次攻击.
        /// 和护甲不同, 闪避是全有全无的, 触发时连元素伤害一起躲掉.
        /// </summary>
        public Stat Evasion;

        // 四种元素抗性, 取值 0~1, 结算时建议钳制在 0.75 上限,
        // 否则装备叠加后会出现完全免疫某系伤害, 那一系的敌人就成了摆设.
        // 和护甲的公式不同: 抗性是直接打折, 伤害 * (1 - 抗性).

        /// <summary>
        /// 火抗, 0~1. 0.3 表示火属性伤害打七折.
        /// </summary>
        public Stat FireRes;

        /// <summary>
        /// 冰抗, 0~1. 0.3 表示冰属性伤害打七折.
        /// </summary>
        public Stat IceRes;

        /// <summary>
        /// 雷抗, 0~1. 0.3 表示雷属性伤害打七折.
        /// </summary>
        public Stat LightningRes;

        /// <summary>
        /// 毒抗, 0~1. 0.3 表示毒属性伤害打七折.
        /// </summary>
        public Stat ToxicRes;
    }
}
