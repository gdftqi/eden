using UnityEngine;
using UnityEngine.Assertions;
using UnityEngine.InputSystem;

namespace Solomon
{
    public class Player : Actor
    {
        public PlayerInputs inputs;
        public Animator anim;

        public PlayerState groundState;

        [SerializeField] public Vector2 moveInputValue;

        protected override void Awake()
        {
            base.Awake();
            inputs = new PlayerInputs();
            Assert.IsNotNull(inputs);

            anim = GetComponent<Animator>();
            Assert.IsNotNull(anim);

            groundState = new PlayerGroundedState(this, stateMachine, "");
        }

        protected override void Start()
        {
            base.Start();
            stateMachine.Init(groundState);
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
        }

        private void OnDisable()
        {
            inputs.Disable();
            inputs.Player.Movement.performed -= OnPlayerMovementInputPerformed;
            inputs.Player.Movement.canceled -= OnPlayerMovementInputCanceled;
        }

        private void OnPlayerMovementInputPerformed(InputAction.CallbackContext ctx)
        {
            moveInputValue = ctx.ReadValue<Vector2>();
        }

        private void OnPlayerMovementInputCanceled(InputAction.CallbackContext ctx)
        {
            moveInputValue = Vector2.zero;
        }
    }
}
