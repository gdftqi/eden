using UnityEngine;

namespace Michael
{
    public class PlayerWallSlideState : EntityState
    {
        public PlayerWallSlideState(Player player, StateMachine sm, string conditionName) : base(player, sm, conditionName)
        {
        }

        public override void Update()
        {
            base.Update();
            HandleWallSlide();

            if (player.inputs.Player.Jump.WasPerformedThisFrame())
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
                player.SetVelocity(player.MoveInputValue.x, player.rb.linearVelocityY);
            }
            else
            {
                player.SetVelocity(player.MoveInputValue.x, player.rb.linearVelocityY * player.WallSlideSlowMultiplier);
            }
        }
    }
}
