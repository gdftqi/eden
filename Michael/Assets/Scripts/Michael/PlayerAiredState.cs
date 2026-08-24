using UnityEngine;


namespace Michael
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
        }
    }
}
