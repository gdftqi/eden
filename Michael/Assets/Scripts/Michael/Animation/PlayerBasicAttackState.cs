using UnityEngine;

namespace Michael.Animation
{
    public class PlayerBasicAttackState : EntityState
    {
        private float attackVelocityTimer = 0f;

        const int StartComboIndex = 1;
        readonly int ComboLimit = 0;

        private int comboIndex = StartComboIndex;
        private float lastAttackedTime = 0f;
        private bool comboAttackQueued = false;
        private float attackDirection = 1f;

        public PlayerBasicAttackState(Player player, StateMachine sm, string conditionName) : base(player, sm, conditionName)
        {
            ComboLimit = player.AttackVelocity.Length;
        }

        public override void Enter()
        {
            base.Enter();
            ResetComboIndex();

            comboAttackQueued = false;
            attackDirection = player.MoveInputValue.x != 0 ? Mathf.Sign(player.MoveInputValue.x) : player.FaceDirection;

            anim.SetInteger("basicAttackIndex", comboIndex);
            ApplyAttackVelocity();
        }

        public override void Exit()
        {
            base.Exit();
            comboIndex++;
            lastAttackedTime = Time.time;
        }

        public override void Update()
        {
            base.Update();

            if (stateMachine.currentState != this)
            {
                return;
            }

            HandleAttackVelocity();

            if (player.inputs.Player.Attack.WasPressedThisFrame())
            {
                QueueNextAttack();
            }

            if (triggerCalled)
            {
                HandleStateExit();
            }
        }

        private void QueueNextAttack()
        {
            if (comboIndex < ComboLimit)
            {
                comboAttackQueued = true;
            }
        }

        private void HandleAttackVelocity()
        {
            attackVelocityTimer -= Time.deltaTime;

            if (attackVelocityTimer <= 0)
            {
                player.SetVelocity(0, player.rb.linearVelocityY);
            }
        }

        private void HandleStateExit()
        {
            if (comboAttackQueued)
            {
                anim.SetBool(stateConditionName, false);
                player.EnterAttackStateWithDelay();
            }
            else
            {
                stateMachine.ChangeState(player.IdleState);
            }
        }

        private void ApplyAttackVelocity()
        {
            attackVelocityTimer = player.AttackVelocityDuration;
            var av = player.AttackVelocity[comboIndex - 1];
            player.SetVelocity(av.x * attackDirection, av.y);
        }

        private void ResetComboIndex()
        {
            if (Time.time > lastAttackedTime + player.ComboResetTime || comboIndex > ComboLimit)
            {
                comboIndex = StartComboIndex;
            }
        }
    }
}
