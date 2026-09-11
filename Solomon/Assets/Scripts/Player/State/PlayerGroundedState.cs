using UnityEngine;
using UnityEngine.Playables;

namespace Solomon
{
    public class PlayerGroundedState : PlayerState
    {
        public PlayerGroundedState(Player player, StateMachine sm, string conditionName) : base(player, sm, conditionName)
        {
        }

        public override void Update()
        {
            base.Update();

            //if (rb.linearVelocity.y < 0 && !player.groundDetected)
            //{
            //    // FALL
            //    return;
            //}

            //if (inputs.Player.Jump.WasPressedThisFrame())
            //{
            //    // JUMP
            //    return;
            //}
        }
    }
}
