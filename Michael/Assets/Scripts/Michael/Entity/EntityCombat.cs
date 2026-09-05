using UnityEngine;

namespace Michael
{
    public class EntityCombat : MonoBehaviour
    {
        private EntityVFX vfx;
        public float Damage = 10f;

        [Header("Target detection")]
        [SerializeField] private Transform targetCheck;
        [SerializeField] private float targetCheckRadius = 1f;
        [SerializeField] private LayerMask whatIsTarget;

        private void Awake()
        {
            vfx = GetComponent<EntityVFX>();
        }

        public void PerformAttack()
        {
            var targets = GetDetectedColliders();
            foreach (var target in targets)
            {
                IDamagable damagable = target.GetComponent<IDamagable>();

                if (damagable == null)
                {
                    continue;
                }

                damagable.TakeDamage(Damage, transform);
                target.GetComponent<ICounterable>()?.HandleCounter();
                vfx?.CreateOnHitVFX(target.transform);
            }
        }

        protected Collider2D[] GetDetectedColliders()
        {
            return Physics2D.OverlapCircleAll(targetCheck.position, targetCheckRadius, whatIsTarget);
        }

        private void OnDrawGizmos()
        {
            Gizmos.DrawWireSphere(targetCheck.position, targetCheckRadius);
        }
    }
}
