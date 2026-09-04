namespace Michael.Animation
{
    public class PlayerGroundedState : PlayerState
    {
        public PlayerGroundedState(Player player, StateMachine sm, string conditionName) : base(player, sm, conditionName)
        {
        }

        public override void Update()
        {
            base.Update();

            if (rb.linearVelocityY < 0 && !player.GroundDetected)
            {
                stateMachine.ChangeState(player.FallState);
                return;
            }

            if (inputs.Player.Jump.WasPressedThisFrame())
            {
                stateMachine.ChangeState(player.JumpState);
                return;
            }

            if (inputs.Player.Attack.WasPressedThisFrame())
            {
                stateMachine.ChangeState(player.BasicAttackState);
                return;
            }

            if (inputs.Player.CounterAttack.WasPressedThisFrame())
            {
                stateMachine.ChangeState(player.CounterAttackState);
                return;
            }
        }
    }
}
