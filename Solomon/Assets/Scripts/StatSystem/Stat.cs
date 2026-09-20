using System;
using UnityEngine;

namespace Solomon
{
    [Serializable]
    public class Stat
    {
        [SerializeField] private float baseValue = 0f;

        // 主属性/装备/天赋提供的加成. 不序列化 -- 每次进游戏由 CharacterStat 重算, 不存盘.
        private float bonus = 0f;

        /// <summary>
        /// 最终值 = 面板基础值 + 加成. 所有消费方都该走这里, 不要直接读 baseValue.
        /// </summary>
        public float GetValue()
        {
            return baseValue + bonus;
        }


        /// <summary>
        /// 覆盖式写入加成, 不是累加. 目前只有 CharacterStat.ApplyMajorBonus 调.
        /// </summary>
        public void SetBonus(float value)
        {
            bonus = value;
        }

        public Stat(float baseValue)
        {
            this.baseValue = baseValue;
        }

        public Stat() { }
    }
}
