using UnityEngine;

namespace Michael
{
    public interface IDamagable
    {
        public void TakeDamage(float damage, Transform damageDealer);
    }
}
