namespace Michael.Animation
{
    public class PlayerGroundedState : EntityState
    {
        public PlayerGroundedState(Player player, StateMachine sm, string conditionName) : base(player, sm, conditionName)
        {
        }

        public override void Update()
        {
            base.Update();

            if (player.rb.linearVelocityY < 0 && !player.GroundDetected)
            {
                stateMachine.ChangeState(player.FallState);
                return;
            }

            if (player.inputs.Player.Jump.WasPerformedThisFrame())
            {
                stateMachine.ChangeState(player.JumpState);
                return;
            }

            if (player.inputs.Player.Attack.WasPerformedThisFrame())
            {
                stateMachine.ChangeState(player.BasicAttackState);
                return;
            }
        }
    }
}
