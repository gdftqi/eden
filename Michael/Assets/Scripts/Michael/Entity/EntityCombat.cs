using System;
using Unity.VisualScripting;
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

        [Header("Status effect details")]
        [SerializeField] private float defaultDuration = 3f;
        [SerializeField] private float chillSlowMultiplier = 0.2f;

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
                IDamagable damegable = target.GetComponent<IDamagable>();

                if (damegable == null)
                {
                    continue;
                }

                if (damegable.TakeDamage(stats.GetPhysicalDamage(out bool isCrit), stats.GetElementalDamage(out ElementType element), element, transform))
                {
                    if (element != ElementType.None)
                    {
                        ApplyStatusEffect(target.transform, element);
                    }

                    // 触发忍杀
                    target.GetComponent<ICounterable>()?.HandleCounter();
                    vfx?.UpdateOnHitColor(element);
                    vfx?.CreateOnHitVFX(target.transform, isCrit);
                }
            }
        }

        public void ApplyStatusEffect(Transform target, ElementType element)
        {
            EntityStatusHandler handler = target.GetComponent<EntityStatusHandler>();
            if (handler == null)
            {
                return;
            }

            if (element == ElementType.Ice && handler.CanBeApplied(ElementType.Ice))
            {
                handler.ApplyChilledEffect(defaultDuration, chillSlowMultiplier);
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
