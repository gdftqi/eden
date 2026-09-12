using UnityEngine;

namespace Solomon
{
    public class PlayerGroundedState : PlayerState
    {
        private const float MOVE_THRESHOLD = 0.1f;
        private const float MOVE_MEMORY = 0.12f;

        private float moveTimer;

        public PlayerGroundedState(Player player, StateMachine sm, string conditionName) : base(player, sm, conditionName)
        {
        }

        public override void Update()
        {
            base.Update();

            if (stateMachine.currentState != this)
            {
                return;
            }

            float vx = player.velocity.x;
            float input = player.moveInputValue.x;

            if (this != player.turnState && input != 0f && Mathf.Sign(input) != player.faceDirection)
            {
                stateMachine.ChangeState(player.turnState);
                return;
            }

            if (Mathf.Abs(vx) > MOVE_THRESHOLD)
            {
                moveTimer = MOVE_MEMORY;
            }
            else
            {
                moveTimer = Mathf.Max(moveTimer - Time.deltaTime, 0f);
            }

            anim.SetBool("move", moveTimer > 0f);
        }
    }
}
