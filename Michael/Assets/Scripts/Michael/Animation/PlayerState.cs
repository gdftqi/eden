using UnityEngine;

namespace Michael.Animation
{
    /// <summary>
    /// 实体状态
    /// </summary>
    public abstract class PlayerState : EntityState
    {
        protected Player player;
        protected PlayerInputs inputs;

        public PlayerState(Player player, StateMachine sm, string conditionName) : base(sm, conditionName)
        {
            this.player = player;
            anim = this.player.Anim;
            inputs = player.inputs;
            rb = player.rb;
        }

        public override void Update()
        {
            base.Update();
            anim.SetFloat("yVelocity", player.rb.linearVelocityY);

            if (inputs.Player.Dash.WasPressedThisFrame() && CanDash())
            {
                stateMachine.ChangeState(player.DashState);
            }
        }

        private bool CanDash()
        {
            if (player.WallDetected)
            {
                return false;
            }

            return true;
        }
    }
}
