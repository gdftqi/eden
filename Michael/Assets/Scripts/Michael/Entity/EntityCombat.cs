using UnityEngine;

namespace Michael
{
    public class EntityCombat : MonoBehaviour
    {
        public float Damage = 10f;

        [Header("Target detection")]
        [SerializeField] private Transform targetCheck;
        [SerializeField] private float targetCheckRadius;
        [SerializeField] private LayerMask whatIsTarget;

        public void PerformAttack()
        {
            GetDetectedColliders();

            var targets = GetDetectedColliders();
            foreach (var target in targets)
            {
                IDamagable damagable = target.GetComponent<IDamagable>();
                damagable?.TakeDamage(Damage, transform);

                target.GetComponent<ICounterable>()?.HandleCoutner();
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
