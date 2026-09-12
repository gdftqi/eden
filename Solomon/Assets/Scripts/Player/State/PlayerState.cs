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
        }
    }
}
