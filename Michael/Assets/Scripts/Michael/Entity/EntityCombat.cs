using UnityEngine;

namespace Michael
{
    public class EntityCombat : MonoBehaviour
    {
        private EntityVFX vfx;
        private EntityStats stats;

        [Header("Target detection")]
        [SerializeField] private Transform targetCheck;
        [SerializeField] private float targetCheckRadius = 1f;
        [SerializeField] private LayerMask whatIsTarget;

        private void Awake()
        {
            vfx = GetComponent<EntityVFX>();
            stats = GetComponent<EntityStats>();
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

                if (damagable.TakeDamage(stats.GetPhysicalDamage(out bool isCrit), transform))
                {
                    // 触发忍杀
                    target.GetComponent<ICounterable>()?.HandleCounter();
                    vfx?.CreateOnHitVFX(target.transform, isCrit);
                }
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
