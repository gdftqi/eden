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
        private const float IntelligenceToElement = 1f;
        private const float ArmorScale = 100f;
        private const float MitigationCap = 0.85f;
        private const float ResistanceCap = 0.75f;


        /// <summary>
        /// 把主属性换算成各副属性的加成
        /// </summary>
        public void ApplyMajorBonus()
        {
            Offense.Damage.SetBonus(Major.Strength.GetValue() * StrengthToDamage);
            Offense.CritChance.SetBonus(Major.Agility.GetValue() * AgilityToCrit);
            Offense.ElementDamage.SetBonus(Major.Intelligence.GetValue() * IntelligenceToElement);
            HP.Max.SetBonus(Major.Vitality.GetValue() * VitalityToHealth);
        }


        /// <summary>
        /// 结算一次攻击
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
                target.OnDamaged(info);
            }
        }


        /// <summary>
        /// 物理减伤率
        /// </summary>
        private float ArmorMitigation(StatDefense def)
        {
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
