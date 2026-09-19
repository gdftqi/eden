using System;

namespace Solomon
{
    public enum ElementType
    {
        None,
        Fire,
        Ice,
        Lightning,
        Toxic
    }


    [Serializable]
    public class StatOffense
    {
        /// <summary>
        /// 基础物理伤害, 一切伤害计算的起点.
        /// </summary>
        public Stat Damage = new Stat(10f);


        /// <summary>
        /// 攻击范围
        /// </summary>
        public Stat AttackRange = new Stat(0f);

        /// <summary>
        /// 暴击倍率. 1.5 表示暴击时打 150% 伤害; 填 1 等于暴击没有意义.
        /// </summary>
        public Stat CritPower = new Stat(1.5f);

        /// <summary>
        /// 暴击概率, 取值 0~1. 0.15 表示 15% 的几率暴击.
        /// </summary>
        public Stat CritChance = new Stat(0.01f);

        /// <summary>
        /// 护甲穿透.
        /// </summary>
        public Stat ArmorReduction = new Stat(0f);

        
        public ElementType Type = ElementType.None;


        public Stat ElementDamage;
    }
}
