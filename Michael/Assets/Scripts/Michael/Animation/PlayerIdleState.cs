namespace Michael.Animation
{
    public class PlayerIdleState : PlayerGroundedState
    {
        public PlayerIdleState(Player player, StateMachine sm, string conditionName) : base(player, sm, conditionName)
        {
        }

        public override void Enter()
        {
            base.Enter();
            player.SetVelocity(0, player.rb.linearVelocityY);
        }

        public override void Exit()
        {
            base.Exit();
        }

        public override void Update()
        {
            base.Update();

            if (stateMachine.currentState != this || player.WallDetected && player.MoveInputValue.x * player.FaceDirection > 0)
            {
                return;
            }

            if (player.MoveInputValue.x != 0)
            {
                stateMachine.ChangeState(player.MoveState);
                return;
            }

            player.SetVelocity(0, player.rb.linearVelocityY);
        }
    }
}
