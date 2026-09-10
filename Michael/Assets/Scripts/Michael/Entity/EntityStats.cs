using UnityEngine;

namespace Michael
{
    public enum EquipmentType
    {
        Sword,
        Helmet,
        Chest,
    }

    public class EntityStats : MonoBehaviour
    {
        public Stat MaxHP;
        public StatMajorGroup major;
        public StatOffenseGroup offense;
        public StatDefenseGroup defense;


        public float GetElementalDamage(out ElementType element, float scaleFactor = 1f)
        {
            float fireDamage = offense.fireDamage.GetValue();
            float iceDamage = offense.iceDamage.GetValue();
            float lightningDamage = offense.lightningDamage.GetValue();
            float bonusElementalDamage = major.intelligence.GetValue();
            float highestDamage = fireDamage;
            element = ElementType.Fire;

            if (iceDamage > highestDamage)
            {
                highestDamage = iceDamage;
                element = ElementType.Ice;
            }

            if (lightningDamage > highestDamage)
            {
                highestDamage = lightningDamage;
                element= ElementType.Lightning;
            }

            if (highestDamage <= 0f)
            {
                element = ElementType.None;
                return 0f;
            }

            float bonusFire = fireDamage == highestDamage ? 0f : fireDamage * 0.5f;
            float bonusIce = iceDamage == highestDamage ? 0f : iceDamage * 0.5f;
            float bonusLightning = lightningDamage == highestDamage ? 0f : lightningDamage * 0.5f;

            float weakerElementsDamage = bonusFire + bonusIce + bonusLightning;
            float finalDamage = highestDamage + bonusElementalDamage + weakerElementsDamage;
            return finalDamage * scaleFactor;
        }

        public float GetElementalResistance(ElementType element)
        {
            float baseResistance = 0f;
            float bonusResistance = major.intelligence.GetValue() * 0.5f;

            switch (element)
            {
                case ElementType.Fire:
                    baseResistance = defense.fireRes.GetValue();
                    break;

                case ElementType.Ice:
                    baseResistance = defense.iceRes.GetValue();
                    break;

                case ElementType.Lightning:
                    baseResistance = defense.lightningRes.GetValue();
                    break;

                default: break;
            }

            var resistance = baseResistance + bonusResistance;
            var resistanceCap = 75f;
            var finalResistance = Mathf.Clamp(resistance, 0f, resistanceCap) / 100f;
            return finalResistance;
        }

        public float GetPhysicalDamage(out bool isCrit, float scaleFactor = 1f)
        {
            float baseDamage = offense.damage.GetValue();
            float bonusDamage = major.strength.GetValue();
            float totalDamage = baseDamage + bonusDamage;

            float baseCritChance = offense.critChance.GetValue();
            float bonusCritChance = major.agility.GetValue() * 0.3f;
            float critChance = baseCritChance + bonusCritChance;

            float baseCritPower = offense.critPower.GetValue();
            float bonusCritPower = major.strength.GetValue() * 0.5f;
            float critPower = (baseCritPower + bonusCritPower) / 100f;

            isCrit = Random.Range(0f, 100f) < critChance;
            var finalDamage = isCrit ? totalDamage * critPower : totalDamage;

            return finalDamage * scaleFactor;
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
