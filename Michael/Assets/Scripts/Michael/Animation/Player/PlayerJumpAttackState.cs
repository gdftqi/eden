namespace Michael.Animation
{
    public class PlayerJumpAttackState : PlayerState
    {
        private bool touchedGround = false;

        public PlayerJumpAttackState(Player player, StateMachine sm, string conditionName) : base(player, sm, conditionName)
        {
        }

        public override void Enter()
        {
            base.Enter();
            touchedGround = false;
            player.SetVelocity(player.JumpAttackVelocity.x * player.FaceDirection, player.JumpAttackVelocity.y);
        }

        public override void Update()
        {
            base.Update();

            if (player.GroundDetected && !touchedGround)
            {
                touchedGround = true;
                anim.SetTrigger("jumpAttackTrigger");
                player.SetVelocity(0, player.rb.linearVelocityY);
            }

            if (triggerCalled && player.GroundDetected)
            {
                stateMachine.ChangeState(player.IdleState);
            }
        }
    }
}
