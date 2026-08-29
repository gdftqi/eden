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
        }

        protected override void Start()
        {
            base.Start();
            stateMachine.Init(IdleState);
        }
    }
}
