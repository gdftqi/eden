using UnityEngine;
using UnityEngine.Assertions;

namespace Michael
{
    public class EntityHealth : MonoBehaviour
    {
        private EntityVFX entityVFX;

        [SerializeField] protected float MaxHP = 100f;
        [SerializeField] protected bool dead = false;

        private void Awake()
        {
            entityVFX = GetComponent<EntityVFX>();
            Assert.IsNotNull(entityVFX);
        }

        public virtual void TakeDamage(float damage, Transform damageDealer)
        {
            if (dead)
            {
                return;
            }

            entityVFX?.PlayOnDamageVFX();
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
