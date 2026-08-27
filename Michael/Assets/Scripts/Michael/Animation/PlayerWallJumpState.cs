namespace Michael.Animation
{
    public class PlayerWallJumpState : EntityState
    {
        public PlayerWallJumpState(Player player, StateMachine sm, string conditionName) : base(player, sm, conditionName)
        {
        }

        public override void Enter()
        {
            base.Enter();

            player.SetVelocity(player.WallJumpForce.x * -player.FaceDirection, player.WallJumpForce.y);
        }

        public override void Update()
        {
            base.Update();

            if (player.rb.linearVelocityY < 0)
            {
                stateMachine.ChangeState(player.FallState);
            }

            if (player.WallDetected)
            {
                stateMachine.ChangeState(player.WallSlideState);
            }
        }
    }
}
