namespace Michael
{
    public class PlayerMoveState : PlayerGroundedState
    {
        public PlayerMoveState(Player player, StateMachine sm, string conditionName) : base(player, sm, conditionName)
        {
        }

        public override void Enter()
        {
            base.Enter();
        }

        public override void Exit()
        {
            base.Exit();
        }

        public override void Update()
        {
            base.Update();

            if (player.MoveInputValue.x == 0)
            {
                stateMachine.ChangeState(player.IdleState);
            }

            player.SetVelocity(player.MoveInputValue.x * player.MoveSpeed);
        }
    }
}
