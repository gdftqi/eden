namespace Michael
{
    public class PlayerJumpState : PlayerAiredState
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

            if (player.rb.linearVelocityY < 0 && stateMachine.currentState != player.JumpAttackState)
            {
                stateMachine.ChangeState(player.FallState);
            }
        }
    }
}
