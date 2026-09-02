namespace Michael.Animation
{
    public class StateMachine
    {
        public EntityState currentState { get; private set; }
        private bool canChangeState;

        public void Init(EntityState startState)
        {
            canChangeState = true;
            currentState = startState;
            currentState.Enter();
        }

        public void ChangeState(EntityState newState)
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
