using UnityEngine;
using UnityEngine.XR;

namespace Michael.Animation
{
    public class EnemyStunnedState : EnemyState
    {
        public EnemyStunnedState(Enemy enemy, StateMachine sm, string conditionName) : base(enemy, sm, conditionName)
        {
        }

        public override void Enter()
        {
            base.Enter();

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
