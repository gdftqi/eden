using UnityEngine;
using UnityEngine.InputSystem;

namespace Solomon
{
    public class PlayerController : Character
    {
        public IA_Player inputs;
        private Vector2 moveInputValue;


        public Vector2 MoveInput
        {
            get { return moveInputValue; }
        }


        public bool Grounded
        {
            get
            {
                return groundDetected;
            }
        }


        public float VerticalSpeed
        {
            get
            {
                return body.linearVelocityY;
            }
        }


        public float HorizontalSpeed
        {
            get
            {
                return body.linearVelocityX;
            }
        }


        private Platform platform;


        protected override void Awake()
        {
            base.Awake();
            inputs = new IA_Player();
        }


        private void Start()
        {
            platform = FindFirstObjectByType<Platform>();
        }


        protected override void Update()
        {
            base.Update();

            if (inputs.Player.Attack.WasPressedThisFrame())
            {
                Attack();
            }

            if (inputs.Player.Jump.WasPressedThisFrame() && groundDetected)
            {
                if (moveInputValue.y < -0.5f && platform != null)
                {
                    IgnoreGroundFor(platform.DropThrough());
                }
                else
                {
                    Jump();
                }
            }

            if (!IsAttacking())
            {
                if (!groundDetected)
                {
                    PlayAnim(body.linearVelocityY > 0f ? "jump" : "fall", false);
                }
                else if (Mathf.Abs(moveInputValue.x) > 0.1f)
                {
                    SetFacing(moveInputValue.x);
                    PlayAnim("run", true);
                }
                else
                {
                    PlayAnim("idle", true);
                }
            }
        }


        protected override void FixedUpdate()
        {
            base.FixedUpdate();
            if (!IsAttacking())
            {
                Move(moveInputValue.x);
            }
            else if (groundDetected)
            {
                Move(0f);
            }
        }


        protected override void OnDestroy()
        {
            base.OnDestroy();
            inputs?.Dispose();
        }


        protected override void OnEnable()
        {
            base.OnEnable();

            inputs.Enable();
            inputs.Player.Movement.performed += OnPlayerMovementPerformed;
            inputs.Player.Movement.canceled += OnPlayerMovementCanceled;
        }


        protected override void OnDisable()
        {
            base.OnDisable();

            inputs.Disable();
            inputs.Player.Movement.performed -= OnPlayerMovementPerformed;
            inputs.Player.Movement.canceled -= OnPlayerMovementCanceled;
        }


        private void OnPlayerMovementPerformed(InputAction.CallbackContext ctx)
        {
            moveInputValue = ctx.ReadValue<Vector2>();
        }


        private void OnPlayerMovementCanceled(InputAction.CallbackContext context)
        {
            moveInputValue = Vector2.zero;
        }
    }
}
