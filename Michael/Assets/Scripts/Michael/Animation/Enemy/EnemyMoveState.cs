using UnityEngine;

namespace Michael.Animation
{
    public class EnemyMoveState : EnemyGroundedState
    {
        public EnemyMoveState(Enemy enemy, StateMachine sm, string conditionName) : base(enemy, sm, conditionName)
        {
        }

        public override void Enter()
        {
            base.Enter();
            if (!enemy.GroundDetected || enemy.WallDetected)
            {
                enemy.Flip();
            }
        }

        public override void Update()
        {
            base.Update();
            enemy.SetVelocity(enemy.moveSpeed * enemy.FaceDirection, rb.linearVelocityY);
            if (!enemy.GroundDetected || enemy.WallDetected)
            {
                stateMachine.ChangeState(enemy.IdleState);
            }
        }
    }
}
