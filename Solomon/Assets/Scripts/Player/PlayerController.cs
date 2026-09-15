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

            if (groundDetected)
            {
                if (inputs.Player.Jump.WasPressedThisFrame())
                {
                    if (moveInputValue.y < -0.5f && platform != null)
                    {
                        IgnoreGroundFor(platform.DropThrough());
                    }
                    else
                    {
                        Jump();
                    }

                    return;
                }

                if (Mathf.Abs(moveInputValue.x) > 0.1f)
                {
                    SetFacing(moveInputValue.x > 0f);
                    PlayAnim("run", true);
                }
                else
                {
                    PlayAnim("idle", true);
                }
            }
            else
            {
                PlayAnim(body.linearVelocityY > 0f ? "jump" : "fall", false);
            }
        }


        protected override void FixedUpdate()
        {
            base.FixedUpdate();
            body.linearVelocityX = moveInputValue.x * moveSpeed;
        }


        protected override void OnDestroy()
        {
            base.OnDestroy();
            inputs?.Dispose();
        }


        private void OnEnable()
        {
            inputs.Enable();
            inputs.Player.Movement.performed += OnPlayerMovementPerformed;
            inputs.Player.Movement.canceled += OnPlayerMovementCanceled;
        }


        private void OnDisable()
        {
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
