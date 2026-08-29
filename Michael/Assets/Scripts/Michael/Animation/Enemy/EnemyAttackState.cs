namespace Michael.Animation
{
    public class EnemyAttackState : EnemyState
    {
        public EnemyAttackState(Enemy enemy, StateMachine sm, string conditionName) : base(enemy, sm, conditionName)
        {
        }

        public override void Update()
        {
            base.Update();

            if (triggerCalled)
            {
                stateMachine.ChangeState(enemy.IdleState);
            }
        }
    }
}
