using UnityEngine;

namespace Solomon
{
    public class PlayerGroundedState : PlayerState
    {
        private const float MOVE_THRESHOLD = 0.1f;
        private float prevVelocityX = 0f;

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

            var x = rb.linearVelocity.x;
            var xs = Mathf.Abs(x);
            anim.SetBool("move", xs > MOVE_THRESHOLD);

            if (x != prevVelocityX && xs == 1f)
            {
                if (prevVelocityX == 0f)
                {
                    // TODO IDLE -> ×ªÉí
                }
                else
                {
                    if (x < 0f)
                    {
                        anim.SetBool("turnLeft", true);
                    }
                    else
                    {
                        anim.SetBool("turnRight", true);
                    }
                }

                prevVelocityX = x;
            }
        }
    }
}
