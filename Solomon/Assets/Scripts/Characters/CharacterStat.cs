using System;
using System.Collections.Generic;
using UnityEngine;

namespace Solomon
{
    public struct DamageInfo
    {
        public ElementType Type;
        public float Damage;
        public bool IsCrit;
    }


    [Serializable]
    public class CharacterStat
    {
        public StatMajor Major;
        public StatOffense Offense;
        public StatDefense Defense;
        public StatLocomotion Locomotion;

        public ResourcePool HP;
        public ResourcePool MP;
        public ResourcePool FP;

        // 平衡系数, 集中放这里, 不要散在方法里
        private const float StrengthToDamage = 1f;
        private const float AgilityToCrit = 0.003f;
        private const float VitalityToHealth = 5f;
        private const float ArmorScale = 100f;
        private const float MitigationCap = 0.85f;
        private const float ResistanceCap = 0.75f;


        /// <summary>
        /// 结算一次攻击: 对每个目标算出最终伤害并扣血.
        /// 暴击对整次攻击掷一次, 不是每个目标各掷一次 -- 横扫时有的暴有的不暴很怪.
        /// 减免要逐个算, 因为每个目标的护甲和抗性不同.
        /// </summary>
        public void ApplyDamage(IList<Character> targets)
        {
            if (targets == null)
            {
                return;
            }

            bool isCrit = UnityEngine.Random.value < Offense.CritChance.GetValue();
            float critPower = isCrit ? Offense.CritPower.GetValue() : 1f;

            float physical = Offense.Damage.GetValue() * critPower;
            float elemental = Offense.ElementDamage.GetValue() * critPower;

            foreach (var target in targets)
            {
                if (target == null || target.stat == null)
                {
                    continue;
                }

                StatDefense def = target.stat.Defense;

                DamageInfo info = new DamageInfo();
                info.Type = Offense.Type;
                info.IsCrit = isCrit;
                info.Damage = physical * (1f - ArmorMitigation(def)) + elemental * (1f - Resistance(def, info.Type));

                target.stat.HP.Reduce(info.Damage);

                // TODO: 这里把 info 交给目标, 用于飘字、受击特效、以及按 Type 挂状态
            }
        }


        /// <summary>
        /// 物理减伤率, 0~MitigationCap. 用 护甲/(护甲+ArmorScale) 这个形状:
        /// 护甲可以无限堆而收益自动递减, 不会出现完全免疫.
        /// ArmorScale 的含义是 "护甲等于这个值时减伤正好 50%".
        /// </summary>
        private float ArmorMitigation(StatDefense def)
        {
            // 穿透按比例削弱对方护甲, 不是直接减固定值
            float penetration = Mathf.Clamp01(Offense.ArmorReduction.GetValue());
            float armor = def.Armor.GetValue() * (1f - penetration);

            if (armor <= 0f)
            {
                return 0f;
            }

            return Mathf.Min(armor / (armor + ArmorScale), MitigationCap);
        }


        /// <summary>
        /// 对应元素的抗性, 0~1. 钳制上限防止装备叠加后完全免疫某一系.
        /// </summary>
        private static float Resistance(StatDefense def, ElementType type)
        {
            float value;

            switch (type)
            {
                case ElementType.Fire: value = def.FireRes.GetValue(); break;
                case ElementType.Ice: value = def.IceRes.GetValue(); break;
                case ElementType.Lightning: value = def.LightningRes.GetValue(); break;
                case ElementType.Toxic: value = def.ToxicRes.GetValue(); break;
                default: return 0f;
            }

            return Mathf.Clamp(value, 0f, ResistanceCap);
        }
    }
}
