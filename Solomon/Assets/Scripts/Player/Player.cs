using UnityEngine;
using UnityEngine.Assertions;
using UnityEngine.InputSystem;

namespace Solomon
{
    public class Player : Actor
    {
        public PlayerInputs inputs;
        public Animator anim;

        public PlayerState idleState;
        public PlayerState moveState;
        public PlayerState dashState;

        [SerializeField] public Vector2 moveInputValue;

        protected override void Awake()
        {
            base.Awake();
            inputs = new PlayerInputs();
            Assert.IsNotNull(inputs);

            anim = GetComponent<Animator>();
            Assert.IsNotNull(anim);

            idleState = new PlayerIdleState(this, stateMachine, "idle");
            moveState = new PlayerMoveState(this, stateMachine, "move");
        }

        protected override void Start()
        {
            base.Start();
            stateMachine.Init(idleState);
        }

        protected override void Update()
        {
            base.Update();
            SetVelocity(moveInputValue.x, rb.linearVelocity.y);
        }

        private void OnEnable()
        {
            inputs.Enable();
            inputs.Player.Movement.performed += OnPlayerMovementInputPerformed;
            inputs.Player.Movement.canceled += OnPlayerMovementInputCanceled;
            //inputs.Player.Jump.performed += OnPlayerJumpPerformed;
        }

        private void OnDisable()
        {
            inputs.Disable();
            inputs.Player.Movement.performed -= OnPlayerMovementInputPerformed;
            inputs.Player.Movement.canceled -= OnPlayerMovementInputCanceled;
            //inputs.Player.Jump.performed -= OnPlayerJumpPerformed;
        }

        private void OnPlayerMovementInputPerformed(InputAction.CallbackContext ctx)
        {
            moveInputValue = ctx.ReadValue<Vector2>();
        }

        private void OnPlayerMovementInputCanceled(InputAction.CallbackContext ctx)
        {
            moveInputValue = Vector2.zero;
        }

        //private void OnPlayerJumpPerformed(InputAction.CallbackContext ctx)
        //{
        //    Jump();
        //}
    }
}
