namespace Michael.Animation
{
    public class EnemyState : EntityState
    {
        protected Enemy enemy;

        public EnemyState(Enemy enemy, StateMachine sm, string conditionName) : base(sm, conditionName)
        {
            this.enemy = enemy;
            rb = enemy.rb;
            anim = enemy.Anim;
        }

        public override void Update()
        {
            base.Update();
            anim.SetFloat("moveAnimSpeedMultiplier", enemy.moveAnimSpeedMultiplier);
            anim.SetFloat("xVelocity", rb.linearVelocityX);
        }
    }
}
