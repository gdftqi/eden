namespace Michael
{
    public class PlayerDashState : EntityState
    {
        private float originalGravityScale;
        private float dashDirection;

        public PlayerDashState(Player player, StateMachine sm, string conditionName) : base(player, sm, conditionName)
        {
        }

        public override void Enter()
        {
            base.Enter();
            stateTimer = player.DashDuration;
            originalGravityScale = player.rb.gravityScale;
            player.rb.gravityScale = 0;
            dashDirection = player.FaceDirection;
        }

        public override void Exit()
        {
            base.Exit();
            player.SetVelocity(0f, 0f);
            player.rb.gravityScale = originalGravityScale;
        }

        public override void Update()
        {
            base.Update();

            if (CancelDash())
            {
                return;
            }

            player.SetVelocity(player.DashSpeed * dashDirection, 0);

            if (stateTimer <= 0f)
            {
                ChangeState();
            }
        }

        private bool CancelDash()
        {
            if (player.WallDetected)
            {
                ChangeState();
                return true;
            }

            return false;
        }

        private void ChangeState()
        {
            if (player.GroundDetected)
            {
                stateMachine.ChangeState(player.IdleState);
            }
            else
            {
                stateMachine.ChangeState(player.FallState);
            }
        }
    }
}
