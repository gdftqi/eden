using UnityEngine;

namespace Michael
{
    public class EntityStats : MonoBehaviour
    {
        public Stat MaxHP;
        public Stat Vitality;


        public float GetMaxHealth()
        {
            float baseHp = MaxHP.GetValue();
            float bonusHp = Vitality.GetValue() * 5;
            return baseHp + bonusHp;
        }
    }
}
