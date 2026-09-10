using System;
using UnityEngine;

namespace Solomon
{
    [Serializable]
    public class Stat
    {
        [SerializeField, Tooltip("基础值. 装备和 buff 的加成不写进这里, 保证脱装备后能原样还原")]
        private float baseValue;

        public Stat(float v = 0f)
        {
            baseValue = v;
        }

        public float GetValue()
        {
            return baseValue;
        }
    }
}
