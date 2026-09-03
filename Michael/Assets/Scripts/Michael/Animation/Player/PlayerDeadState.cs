using UnityEngine;

namespace Michael.Animation
{
    public class PlayerDeadState : PlayerState
    {
        public PlayerDeadState(Player player, StateMachine sm, string conditionName) : base(player, sm, conditionName)
        {
        }

        public override void Enter()
        {
            base.Enter();
            inputs.Disable();
            rb.simulated = false;
        }
    }
}
