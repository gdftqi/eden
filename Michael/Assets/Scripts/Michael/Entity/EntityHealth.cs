using UnityEngine;
using UnityEngine.UI;

namespace Michael
{
    public class EntityHealth : MonoBehaviour, IDamagable
    {
        private Slider healthBar;
        private EntityVFX entityVFX;
        private EntitySFX entityAudio;
        private Entity entity;
        private EntityStats stats;

        [SerializeField] protected float CurrentHP = 0f;
        [SerializeField] protected bool dead = false;

        [Header("On Damage Knockback")]
        [SerializeField] protected Vector2 KnockbackPower = new Vector2(1.5f, 2.5f);
        [SerializeField] protected float KnockbackDuration = 0.15f;
        [SerializeField] protected Vector2 HeavyKnockbackPower = new Vector2(7f, 7f);
        [SerializeField] protected float HeavyKnockbackDuration = 0.5f;

        [Header("On Heavy Damage")]
        [Range(0f, 1f)]
        [SerializeField] protected float HeavyDamageThreshold = 0.3f;

        private void Awake()
        {
            entityVFX = GetComponent<EntityVFX>();
            entityAudio = GetComponent<EntitySFX>();
            entity = GetComponent<Entity>();
            healthBar = GetComponentInChildren<Slider>();
            stats = GetComponent<EntityStats>();
                
            CurrentHP = stats!.GetMaxHealth(); 
            UpdateHealthBar();
        }

        public virtual void TakeDamage(float damage, Transform damageDealer)
        {
            if (dead)
            {
                return;
            }

            var (knockback, duration) = CalculateKnockback(damage, damageDealer);
            entity.ReceiveKnockback(knockback, duration);
            entityVFX?.PlayOnDamageVFX();
            entityAudio?.PlayOnDamageSFX();
            ReduceHP(damage);
        }

        protected void ReduceHP(float damage)
        {
            CurrentHP -= damage;
            UpdateHealthBar();
            if (CurrentHP <= 0)
            {
                Die();
            }
        }

        private void Die()
        {
            dead = true;
            entity.EntityDead();
        }

        private (Vector2, float) CalculateKnockback(float damage, Transform damageDealer)
        {
            var direction = transform.position.x > damageDealer.position.x ? 1f : -1f;
            Vector2 knockback;
            float duration = 0f;

            if (IsHeavyDamage(damage))
            {
                knockback = HeavyKnockbackPower;
                duration = HeavyKnockbackDuration;
            }
            else
            {
                knockback = KnockbackPower;
                duration = KnockbackDuration;
            }

            knockback.x *= direction;
            return (knockback, duration);
        }

        private bool IsHeavyDamage(float damage)
        {
            return damage / stats.GetMaxHealth() >= HeavyDamageThreshold;
        }

        private void UpdateHealthBar()
        {
            if (healthBar != null)
            {
                healthBar.value = CurrentHP / stats.GetMaxHealth();
            }
        }
    }
}
