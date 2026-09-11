using UnityEngine;

namespace Solomon
{
    public abstract class ActorState
    {
        protected StateMachine stateMachine;
        protected string stateConditionName;
        protected Animator anim;
        protected Rigidbody rb;

        protected float stateTimer = 0f;
        protected bool triggerCalled = false;

        public ActorState(StateMachine sm, string conditionName)
        {
            stateMachine = sm;
            stateConditionName = conditionName;
        }

        public virtual void Enter()
        {
            // conditionName 为空表示这个状态不需要自己的 Animator bool (比如动画由别的参数驱动).
            if (!string.IsNullOrEmpty(stateConditionName))
            {
                anim.SetBool(stateConditionName, true);
            }

            triggerCalled = false;
        }

        public virtual void Exit()
        {
            if (!string.IsNullOrEmpty(stateConditionName))
            {
                anim.SetBool(stateConditionName, false);
            }
        }

        public virtual void Update()
        {
            stateTimer -= Time.deltaTime;
        }

        public void CallAnimationTrigger()
        {
            triggerCalled = true;
        }
    }
}
