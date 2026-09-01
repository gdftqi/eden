using UnityEngine;

namespace Michael
{
    public class EntityHealth : MonoBehaviour
    {
        [SerializeField] protected float MaxHP = 100f;
        [SerializeField] protected bool dead = false;

        public virtual void TakeDamage(float damage, Transform damageDealer)
        {
            if (dead)
            {
                return;
            }

            ReduceHP(damage);
        }

        protected void ReduceHP(float damage)
        {
            MaxHP -= damage;
            if (MaxHP <= 0)
            {
                Die();
            }
        }

        private void Die()
        {
            dead = true;
        }
    }
}
