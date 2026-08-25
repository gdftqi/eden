

using UnityEngine;

namespace Michael
{
    /// <summary>
    /// 实体状态
    /// </summary>
    public abstract class EntityState
    {
        protected Player player;
        protected StateMachine stateMachine;
        protected string stateConditionName;
        protected Animator anim;

        protected float stateTimer = 0f;
        protected bool triggerCalled = false;


        public EntityState(Player player, StateMachine sm, string conditionName)
        {
            this.player = player;
            stateMachine = sm;
            stateConditionName = conditionName;
            anim = player.Anim;
        }

        /// <summary>
        /// 每次当即将进入到某个状态时, 调用该方法
        /// </summary>
        public virtual void Enter()
        {
            anim.SetBool(stateConditionName, true);
            triggerCalled = false;
        }

        /// <summary>
        /// 每次当即将进入下一个状态时, 调用该方法
        /// </summary>
        public virtual void Exit()
        {
            anim.SetBool(stateConditionName, false);
        }

        /// <summary>
        /// 逻辑更新
        /// </summary>
        public virtual void Update()
        {
            stateTimer -= Time.deltaTime;

            anim.SetFloat("yVelocity", player.rb.linearVelocityY);

            if (player.inputs.Player.Dash.WasPressedThisFrame() && CanDash())
            {
                stateMachine.ChangeState(player.DashState);
            }
        }

        public void CallAnimationTrigger()
        {
            triggerCalled = true;
        }

        private bool CanDash()
        {
            if (player.WallDetected)
            {
                return false;
            }

            return true;
        }
    }
}
