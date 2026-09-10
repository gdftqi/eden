using UnityEngine;

namespace Michael
{
    public class Chest : MonoBehaviour, IDamagable
    {
        private Animator anim
        {
            get
            {
                return GetComponentInChildren<Animator>();
            }
        }

        private Rigidbody2D rb
        {
            get
            {
                return GetComponent<Rigidbody2D>();
            }
        }

        private EntityVFX vfx
        {
            get
            {
                return GetComponent<EntityVFX>();
            }
        }

        private EntitySFX sfx
        {
            get
            {
                return GetComponent<EntitySFX>();
            }
        }

        [Header("Open Details")]
        [SerializeField] private Vector2 knockback;

        public bool TakeDamage(float damage, float elementalDamage, ElementType element, Transform damageDealer)
        {
            vfx?.PlayOnDamageVFX();
            sfx?.PlayOnDamageSFX();
            anim?.GetComponentInChildren<Animator>().SetBool("chestOpen", true);
            rb.linearVelocity = knockback;
            rb.angularVelocity = Random.Range(-200f, 200f);

            return true;
        }
    }
}
