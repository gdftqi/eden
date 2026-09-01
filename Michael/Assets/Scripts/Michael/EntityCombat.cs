using UnityEngine;

namespace Michael
{
    public class EntityCombat : MonoBehaviour
    {
        public Collider2D[] targetColliders;

        [Header("Target detection")]
        [SerializeField] private Transform targetCheck;
        [SerializeField] private float targetCheckRadius;
        [SerializeField] private LayerMask whatIsTarget;

        public void PerformAttack()
        {
            GetDetectedColliders();

            foreach (var collider in targetColliders)
            {

            }
        }

        private void GetDetectedColliders()
        {
            targetColliders = Physics2D.OverlapCircleAll(targetCheck.position, targetCheckRadius, whatIsTarget);
        }

        private void OnDrawGizmos()
        {
            Gizmos.DrawWireSphere(targetCheck.position, targetCheckRadius);
        }
    }
}
