using UnityEngine;

namespace Michael.Animation
{
    public class PlayerCounterAttackState : PlayerState
    {
        private PlayerCombat combat;
        private bool counteredSomebody = false;

        public PlayerCounterAttackState(Player player, StateMachine sm, string conditionName) : base(player, sm, conditionName)
        {
            combat = player.GetComponent<PlayerCombat>();
        }

        public override void Enter()
        {
            base.Enter();

            stateTimer = combat?.GetCounterRecoveryDuration() ?? 0f;
            counteredSomebody = combat.CounterAttackPerformed();
            anim.SetBool("counterAttackPerformed", counteredSomebody);

            if (counteredSomebody)
            {
                player.GetComponent<EntitySFX>()?.PlayOnCounterSFX();
            }
        }

        public override void Update()
        {
            base.Update();

            player.SetVelocity(0f, rb.linearVelocityY);

            if (triggerCalled || (stateTimer <= 0f && !counteredSomebody))
            {
                stateMachine.ChangeState(player.IdleState);
            }
        }
    }
}
