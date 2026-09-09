using UnityEngine;

namespace Michael
{
    public interface IDamagable
    {
        public bool TakeDamage(float damage, Transform damageDealer);
    }
}
