using UnityEngine;


namespace Michael.Animation
{
    public class PlayerAiredState : EntityState
    {
        public PlayerAiredState(Player player, StateMachine sm, string conditionName) : base(player, sm, conditionName)
        {
        }

        public override void Update()
        {
            base.Update();

            if (player.MoveInputValue.x != 0)
            {
                player.SetVelocity(player.MoveInputValue.x * player.MoveSpeed * player.InAirMoveMultiplier, player.rb.linearVelocityY);
            }

            if (player.inputs.Player.Attack.WasPressedThisFrame())
            {
                stateMachine.ChangeState(player.JumpAttackState);
            }
        }
    }
}
