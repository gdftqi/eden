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

        public void EnableCounterWindow()
        {
            enemy?.EnableCounterWindow(true);
        }

        public void DisableCounterWindow()
        {
            enemy?.EnableCounterWindow(false);
        }
    }
}
