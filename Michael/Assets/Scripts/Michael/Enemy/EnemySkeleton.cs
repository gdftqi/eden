using Michael.Animation;

namespace Michael
{
    public class EnemySkeleton : Enemy
    {
        protected override void Awake()
        {
            base.Awake();
            IdleState = new EnemyIdleState(this, stateMachine, "idle");
            MoveState = new EnemyMoveState(this, stateMachine, "move");
            AttackState = new EnemyAttackState(this, stateMachine, "attack");
            BattleState = new EnemyBattleState(this, stateMachine, "battle");
            DeadState = new EnemyDeadState(this, stateMachine, "idle");
        }

        protected override void Start()
        {
            base.Start();
            stateMachine.Init(IdleState);
        }
    }
}
