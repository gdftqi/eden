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
            counteredSomebody = false;
            anim.SetBool("counterAttackPerformed", false);
            stateTimer = combat?.GetCounterDuration() ?? 0f;
        }

        public override void Update()
        {
            base.Update();

            if (combat.CounterAttackPerformed())
            {
                counteredSomebody = true;
                anim.SetBool("counterAttackPerformed", counteredSomebody);
                return;
            }

            if (triggerCalled || (stateTimer <= 0f && !counteredSomebody))
            {
                stateMachine.ChangeState(player.IdleState);
            }
        }
    }
}
