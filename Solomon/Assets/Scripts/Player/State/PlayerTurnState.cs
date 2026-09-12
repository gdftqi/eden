using UnityEngine;

namespace Solomon
{
    public class PlayerTurnState : PlayerGroundedState
    {
        private const float FALLBACK_TIMEOUT = 2f;

        public PlayerTurnState(Player player, StateMachine sm, string conditionName)
            : base(player, sm, conditionName)
        {
        }

        public override void Enter()
        {
            base.Enter();
            stateTimer = FALLBACK_TIMEOUT;
            player.animationDrivenRotation = true;
            player.Flip();
        }


        public override void Exit()
        {
            base.Exit();

            player.animationDrivenRotation = false;
        }


        public override void Update()
        {
            base.Update();

            if (stateMachine.currentState != this)
            {
                return;
            }

            if (triggerCalled || stateTimer <= 0f)
            {
                stateMachine.ChangeState(player.groundState);
            }
        }
    }
}
