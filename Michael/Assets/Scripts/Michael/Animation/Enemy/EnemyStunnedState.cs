using UnityEngine;
using UnityEngine.XR;

namespace Michael.Animation
{
    public class EnemyStunnedState : EnemyState
    {
        private EnemyVFX vfx;

        public EnemyStunnedState(Enemy enemy, StateMachine sm, string conditionName) : base(enemy, sm, conditionName)
        {
            vfx = enemy.GetComponent<EnemyVFX>();
        }

        public override void Enter()
        {
            base.Enter();

            vfx.EnableAttackAlert(false);
            enemy.EnableCounterWindow(false);
            stateTimer = enemy.StunnedDuration;
            rb.linearVelocity = new Vector2(enemy.StunnedVelocity.x * -enemy.FaceDirection, enemy.StunnedVelocity.y);
        }

        public override void Update()
        {
            base.Update();
            if (stateTimer <= 0)
            {
                stateMachine.ChangeState(enemy.IdleState);
            }
        }
    }
}
