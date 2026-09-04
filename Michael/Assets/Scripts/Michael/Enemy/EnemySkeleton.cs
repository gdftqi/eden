using Michael.Animation;
using UnityEngine;

namespace Michael
{
    public class EnemySkeleton : Enemy, ICounterable
    {
        public bool CanBeCountered
        {
            get
            {
                return CanBeStunned;
            }
        }

        protected override void Awake()
        {
            base.Awake();
            IdleState = new EnemyIdleState(this, stateMachine, "idle");
            MoveState = new EnemyMoveState(this, stateMachine, "move");
            AttackState = new EnemyAttackState(this, stateMachine, "attack");
            BattleState = new EnemyBattleState(this, stateMachine, "battle");
            DeadState = new EnemyDeadState(this, stateMachine, "idle");
            StunnedState = new EnemyStunnedState(this, stateMachine, "stunned");
        }

        protected override void Start()
        {
            base.Start();
            stateMachine.Init(IdleState);
        }

        public void HandleCounter()
        {
            if (!CanBeCountered)
            {
                return;
            }

            stateMachine.ChangeState(StunnedState);
        }
    }
}
