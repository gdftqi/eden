using System;
using UnityEngine;

namespace Solomon
{
    [Serializable]
    public class StatMajorGroup
    {
        [Tooltip("力量")]
        public Stat strength;

        [Tooltip("敏捷")]
        public Stat agility;

        [Tooltip("智力")]
        public Stat intelligence;

        [Tooltip("体力")]
        public Stat vitality;
    }
}
