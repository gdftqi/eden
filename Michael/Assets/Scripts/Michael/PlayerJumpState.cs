namespace Michael
{
    public class PlayerJumpState : EntityState
    {
        public PlayerJumpState(Player player, StateMachine sm, string conditionName) : base(player, sm, conditionName)
        {
        }

        public override void Enter()
        {
            base.Enter();

            player.SetVelocity(player.rb.linearVelocityX, player.JumpForce);
        }

        public override void Exit()
        {
            base.Exit();
        }

        public override void Update()
        {
            base.Update();

            if (player.rb.linearVelocityY < 0)
            {
                stateMachine.ChangeState(player.FallState);
            }
        }
    }
}
