using UnityEngine;

namespace Solomon
{
    public class PlayerTurnState : PlayerGroundedState
    {
        public PlayerTurnState(Player player, StateMachine sm, string conditionName) : base(player, sm, conditionName)
        {
        }

        public override void Enter()
        {
            base.Enter();
            //stateTimer = 
            player.Flip();
        }
    }
}
