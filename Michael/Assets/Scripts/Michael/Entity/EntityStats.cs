using UnityEngine;

namespace Michael
{
    public class EntityStats : MonoBehaviour
    {
        public float MaxHP = 100;
        public float Vitality = 7;


        public float GetMaxHealth()
        {
            float baseHp = MaxHP;
            float bonusHp = Vitality * 5;
            return baseHp + bonusHp;
        }
    }
}
