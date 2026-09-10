using System;
using UnityEngine;

namespace Solomon
{
    [Serializable]
    public class StatMajorGroup
    {
        [Tooltip("力量: 加物理伤害, 并按 50% 加暴击伤害")]
        public Stat strength;

        [Tooltip("敏捷: 按 30% 加暴击率, 按 50% 加闪避")]
        public Stat agility;

        [Tooltip("智力: 加元素伤害, 并按 50% 加元素抗性")]
        public Stat intelligence;

        [Tooltip("体力: 加护甲, 并按 5 倍加最大生命值")]
        public Stat vitality;
    }
}
