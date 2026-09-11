using UnityEngine;

namespace Solomon
{
    public class PlayerMoveState : PlayerGroundedState
    {
        public PlayerMoveState(Player player, StateMachine sm, string conditionName) : base(player, sm, conditionName)
        {
        }

        public override void Update()
        {
            base.Update();

            if (stateMachine.currentState != this)
            {
                return;
            }

            if (player.moveInputValue.x == 0 || (player.wallDetected && player.moveInputValue.x * player.faceDirection > 0))
            {
                stateMachine.ChangeState(player.idleState);
                return;
            }

            player.SetVelocity(player.moveInputValue.x * player.locomotion.moveSpeed.GetValue(), player.rb.linearVelocity.y);
        }
    }
}
