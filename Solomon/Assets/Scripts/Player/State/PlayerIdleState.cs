using UnityEngine;

namespace Solomon
{
    public class PlayerIdleState : PlayerGroundedState
    {
        public PlayerIdleState(Player player, StateMachine sm, string conditionName) : base(player, sm, conditionName)
        {
        }

        public override void Enter()
        {
            base.Enter();
            player.SetVelocity(0f, rb.linearVelocity.y);
        }

        public override void Exit()
        {
            base.Exit();
        }

        public override void Update()
        {
            base.Update();

            if (stateMachine.currentState != this || player.wallDetected && player.moveInputValue.x * player.faceDirection > 0)
            {
                return;
            }

            if (player.moveInputValue.x != 0)
            {
                stateMachine.ChangeState(player.moveState);
                return;
            }

            player.SetVelocity(0f, rb.linearVelocity.y);
        }
    }
}
