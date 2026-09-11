using UnityEngine;

namespace Solomon
{
    public class PlayerState : ActorState
    {
        protected Player player;
        protected PlayerInputs inputs;

        public PlayerState(Player player, StateMachine sm, string conditionName) : base(sm, conditionName)
        {
            this.player = player;
            anim = player.anim;
            inputs = player.inputs;
            rb = player.rb;
        }

        public override void Enter()
        {
            base.Enter();
            anim.SetFloat("yVelocity", player.rb.linearVelocity.y);

            if (inputs.Player.Dash.WasPressedThisFrame() && CanDash())
            {
                stateMachine.ChangeState(player.dashState);
            }
        }

        private bool CanDash()
        {
            return !player.wallDetected;
        }
    }
}
