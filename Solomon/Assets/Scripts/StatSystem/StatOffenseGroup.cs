using System;
using UnityEngine;

namespace Solomon
{
    [Serializable]
    public class StatOffenseGroup
    {
        public StatOffenseGroup()
        {
        }

        [Tooltip("物理伤害基础值")]
        public Stat damage;

        [Tooltip("暴击伤害, 百分比. 150 表示暴击打 1.5 倍")]
        public Stat critPower;

        [Tooltip("暴击率, 百分比")]
        public Stat critChance;

        [Tooltip("护甲穿透, 百分比. 按比例削减目标的有效护甲")]
        public Stat armorReduction;

        [Tooltip("火焰伤害")]
        public Stat fireDamage;

        [Tooltip("冰霜伤害")]
        public Stat iceDamage;

        [Tooltip("闪电伤害")]
        public Stat lightningDamage;
    }
}
