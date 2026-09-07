using UnityEngine;

namespace Michael
{
    public class EntityStats : MonoBehaviour
    {
        public Stat MaxHP;
        public StatMajorGroup major;
        public StatOffenseGroup offense;
        public StatDefenseGroup defense;

        public float GetMaxHealth()
        {
            float baseHp = MaxHP.GetValue();
            float bonusHp = major.vitality.GetValue() * 5;
            return baseHp + bonusHp;
        }
    }
}
