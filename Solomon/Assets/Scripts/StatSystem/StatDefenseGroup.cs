using System;
using UnityEngine;

namespace Solomon
{
    [Serializable]
    public class StatDefenseGroup
    {
        [Tooltip("护甲: 减免物理伤害, 走递减公式, 上限 85%")]
        public Stat armor;

        [Tooltip("闪避: 完全免除一次伤害的概率, 百分比, 上限 85")]
        public Stat evasion;

        [Tooltip("火焰抗性: 百分比, 上限 75")]
        public Stat fireRes;

        [Tooltip("冰霜抗性: 百分比, 上限 75")]
        public Stat iceRes;

        [Tooltip("闪电抗性: 百分比, 上限 75")]
        public Stat lightningRes;
    }
}
