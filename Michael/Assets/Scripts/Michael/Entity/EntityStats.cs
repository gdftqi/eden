using UnityEngine;

namespace Michael
{
    public class EntityStats : MonoBehaviour
    {
        public Stat MaxHP;
        public StatMajorGroup major;
        public StatOffenseGroup offense;
        public StatDefenseGroup defense;

        public float GetPhysicalDamage(out bool isCrit)
        {
            float baseDamage = offense.damage.GetValue();
            float bonusDamage = major.strength.GetValue();
            float totalDamage = baseDamage + bonusDamage;

            float baseCritChance = offense.critChance.GetValue();
            float bonusCritChance = major.agility.GetValue() * 0.3f;
            float critChance = baseCritChance + bonusCritChance;

            float baseCritPower = offense.critPower.GetValue();
            float bonusCritPower = major.strength.GetValue() * 0.5f;
            float critPower = (baseCritPower + bonusCritPower) / 100;

            isCrit = Random.Range(0f, 100f) < critChance;

            return isCrit ? totalDamage * critPower : totalDamage;
        }

        public float GetArmorMitigation(float armorReduction)
        {
            float baseArmor = defense.armor.GetValue();
            float bonusArmor = major.vitality.GetValue();
            float totalArmor = baseArmor + bonusArmor;

            float reductionMultiplier = Mathf.Clamp01(1f - armorReduction);
            float effectiveArmor = totalArmor * reductionMultiplier;

            float mitigation = effectiveArmor / (totalArmor + 100);
            float mitigationCap = 0.85f;
            float finalMitigation = Mathf.Clamp(mitigation, 0f, mitigationCap);

            return finalMitigation;
        }

        public float GetArmorReduction()
        {
            float finalReduction = offense.armorReduction.GetValue() / 100f;
            return finalReduction;
        }

        public float GetMaxHealth()
        {
            float baseHp = MaxHP.GetValue();
            float bonusHp = major.vitality.GetValue() * 5f;
            return baseHp + bonusHp;
        }

        public float GetEvasion()
        {
            float baseEvasion = defense.evasion.GetValue();
            float bonusEvasion = major.agility.GetValue() * 0.5f;
            float v = baseEvasion + bonusEvasion;
            float evasionCap = 85f;
            return Mathf.Clamp(v, 0, evasionCap);
        }
    }
}
