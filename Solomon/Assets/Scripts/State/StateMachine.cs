using UnityEngine;

namespace Solomon
{
    public class StateMachine
    {
        public ActorState currentState { get; private set; }
        private bool canChangeState = false;

        public void Init(ActorState startState)
        {
            canChangeState = true;
            currentState = startState;
            currentState.Enter();
        }

        public void ChangeState(ActorState newState)
        {
            if (canChangeState)
            {
                currentState.Exit();
                currentState = newState;
                currentState.Enter();
            }
        }

        public void UpdateActiveState()
        {
            currentState.Update();
        }

        public void SwitchOffStateMachine()
        {
            canChangeState = false;
        }
    }
}
