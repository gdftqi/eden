using UnityEngine;

namespace Michael
{
    public interface IDamagable
    {
        public bool TakeDamage(float damage, float elementalDamage, ElementType element, Transform damageDealer);
    }
}
