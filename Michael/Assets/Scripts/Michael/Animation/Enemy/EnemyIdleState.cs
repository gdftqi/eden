using UnityEngine;

namespace Michael.Animation
{
    public class EnemyIdleState : EnemyGroundedState
    {
        public EnemyIdleState(Enemy enemy, StateMachine sm, string conditionName) : base(enemy, sm, conditionName)
        {
        }

        public override void Enter()
        {
            base.Enter();
            stateTimer = enemy.idleTime;
        }

        public override void Update()
        {
            base.Update();
            if (stateMachine.currentState != this)
            {
                return;
            }

            if (stateTimer < 0)
            {
                stateMachine.ChangeState(enemy.MoveState);
            }
        }
    }
}
