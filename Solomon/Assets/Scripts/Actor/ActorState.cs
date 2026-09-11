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
            anim.SetBool(stateConditionName, true);
            triggerCalled = false;
        }

        public virtual void Exit()
        {
            anim.SetBool(stateConditionName, false);
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
