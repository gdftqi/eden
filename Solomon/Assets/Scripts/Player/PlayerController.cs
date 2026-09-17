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

            // 动画优先级: 攻击 > 空中 > 地面移动 > 待机
            if (IsAttacking())
            {
                // 攻击动画正在播, 不让任何状态覆盖它
            }
            else if (!groundDetected)
            {
                PlayAnim(body.linearVelocityY > 0f ? "jump" : "fall", false);
            }
            else if (Mathf.Abs(moveInputValue.x) > 0.1f)
            {
                SetFacing(moveInputValue.x > 0f);
                PlayAnim("run", true);
            }
            else
            {
                PlayAnim("idle", true);
            }
        }


        protected override void FixedUpdate()
        {
            base.FixedUpdate();
            // 攻击时不接受移动输入, 目标速度给 0, 靠 Deceleration() 滑停
            Move(IsAttacking() ? 0f : moveInputValue.x);
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
