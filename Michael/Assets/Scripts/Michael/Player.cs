using Michael.Animation;
using System.Collections;
using UnityEngine;
using UnityEngine.Assertions;

namespace Michael
{
    public class Player : Entity
    {
        [Header("Locomotion properties")]
        public float MoveSpeed = 5f;
        public float JumpForce = 16f;
        public Vector2 WallJumpForce = new Vector2(5f, 12f);
        [Range(0, 1)]
        public float InAirMoveMultiplier = 0.7f;
        [Range(0, 1)]
        public float WallSlideSlowMultiplier = 0.5f;
        [Space]
        public float DashDuration = 0.15f;
        public float DashSpeed = 20f;

        [Header("Attack properties")]
        public Vector2[] AttackVelocity = new Vector2[3]
        {
            new Vector2(3f, 1.5f),
            new Vector2(1f, 2f),
            new Vector2(5f, 1.5f),
        };
        public Vector2 JumpAttackVelocity = new Vector2(5f, 6f);
        public float AttackVelocityDuration = 0.1f;
        public float ComboResetTime = 1f;
        private Coroutine queuedAttackCo;

        internal PlayerInputs inputs;
        internal Vector2 MoveInputValue { get; private set; }

        internal PlayerState IdleState { get; private set; }
        internal PlayerState MoveState { get; private set; }
        internal PlayerState JumpState { get; private set; }
        internal PlayerState FallState { get; private set; }
        internal PlayerState WallSlideState { get; private set; }
        internal PlayerState WallJumpState { get; private set; }
        internal PlayerState DashState { get; private set; }
        internal PlayerState BasicAttackState { get; private set; }
        internal PlayerState JumpAttackState { get; private set; }

        protected override void Awake()
        {
            base.Awake();

            inputs = new PlayerInputs();
            Assert.IsNotNull(inputs);

            IdleState = new PlayerIdleState(this, stateMachine, "idle");
            MoveState = new PlayerMoveState(this, stateMachine, "move");
            JumpState = new PlayerJumpState(this, stateMachine, "jumpFall");
            FallState = new PlayerFallState(this, stateMachine, "jumpFall");
            WallSlideState = new PlayerWallSlideState(this, stateMachine, "wallSlide");
            WallJumpState = new PlayerWallJumpState(this, stateMachine, "jumpFall");
            DashState = new PlayerDashState(this, stateMachine, "dash");
            BasicAttackState = new PlayerBasicAttackState(this, stateMachine, "basicAttack");
            JumpAttackState = new PlayerJumpAttackState(this, stateMachine, "jumpAttack");
        }

        protected override void Start()
        {
            base.Start();
            stateMachine.Init(IdleState);
        }

        private void OnEnable()
        {
            inputs.Enable();
            inputs.Player.Movement.performed += ctx => MoveInputValue = ctx.ReadValue<Vector2>();
            inputs.Player.Movement.canceled += ctx => MoveInputValue = Vector2.zero;
        }

        private void OnDisable()
        {
            inputs.Disable();
        }

        public void EnterAttackStateWithDelay()
        {
            if (queuedAttackCo != null)
            {
                StopCoroutine(queuedAttackCo);
            }

            queuedAttackCo = StartCoroutine(EnterAttackStateWithDelayCo());
        }

        private IEnumerator EnterAttackStateWithDelayCo()
        {
            yield return new WaitForEndOfFrame();
            stateMachine.ChangeState(BasicAttackState);
        }
    }
}
