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

                if (damegable.TakeDamage(stats.GetPhysicalDamage(out bool isCrit, 2f), stats.GetElementalDamage(out ElementType element, 0.6f), element, transform))
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

        public void ApplyStatusEffect(Transform target, ElementType element, float scaleFactor = 1f)
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

            if (element == ElementType.Fire && handler.CanBeApplied(ElementType.Fire))
            {
                float fireDamage = stats.offense.fireDamage.GetValue() * scaleFactor;
                handler.ApplyBurnEffect(defaultDuration, fireDamage);
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
