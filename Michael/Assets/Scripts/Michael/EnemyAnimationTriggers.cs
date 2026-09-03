using Michael.Animation;
using UnityEngine;

namespace Michael
{
    public class EnemyAnimationTriggers : EntityAnimationTriggers
    {
        private Enemy enemy;

        protected override void Awake()
        {
            base.Awake();
            enemy = GetComponentInParent<Enemy>();
        }

        private void EnableCounterWindow()
        {
            enemy?.EnableCounterWindow(true);
        }

        private void DisableCounterWindow()
        {
            enemy?.EnableCounterWindow(false);
        }
    }
}
