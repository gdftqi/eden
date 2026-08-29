namespace Michael.Animation
{
    public class PlayerWallSlideState : PlayerState
    {
        public PlayerWallSlideState(Player player, StateMachine sm, string conditionName) : base(player, sm, conditionName)
        {
        }

        public override void Update()
        {
            base.Update();
            HandleWallSlide();

            if (inputs.Player.Jump.WasPerformedThisFrame())
            {
                stateMachine.ChangeState(player.WallJumpState);
            }

            if (!player.WallDetected)
            {
                stateMachine.ChangeState(player.FallState);
            }

            if (player.GroundDetected)
            {
                stateMachine.ChangeState(player.IdleState);
                player.Flip();
            }
        }

        private void HandleWallSlide()
        {
            if (player.MoveInputValue.y < 0)
            {
                player.SetVelocity(player.MoveInputValue.x, rb.linearVelocityY);
            }
            else
            {
                player.SetVelocity(player.MoveInputValue.x, rb.linearVelocityY * player.WallSlideSlowMultiplier);
            }
        }
    }
}
