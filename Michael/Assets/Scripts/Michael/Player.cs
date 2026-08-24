using UnityEngine;
using UnityEngine.Assertions;


namespace Michael
{
    public class Player : MonoBehaviour
    {
        [Header("Locomotion properties")]
        public float MoveSpeed = 5f;
        public float JumpForce = 12f;
        public Vector2 WallJumpForce = new Vector2(5f, 12f);

        [Range(0, 1)]
        public float InAirMoveMultiplier = 0.7f; // 从 0 到 1

        [Range(0, 1)]
        public float WallSlideSlowMultiplier = 0.5f; // 从 0 到 1

        [Header("Collision detection")]
        [SerializeField] private float checkGroundDistance = 1.35f; // 高度检测长度
        [SerializeField] private float checkWallDistance = 0.55f;
        [SerializeField] private LayerMask whatIsGround;
        

        internal PlayerInputs inputs;
        internal Rigidbody2D rb {  get; private set; }

        private StateMachine stateMachine;
        internal Animator Anim { get; private set; }
        internal EntityState IdleState { get; private set; }
        internal EntityState MoveState { get; private set; }
        internal EntityState JumpState { get; private set; }
        internal EntityState FallState {  get; private set; }
        internal EntityState WallSlideState {  get; private set; }
        internal EntityState WallJumpState { get; private set; }


        internal Vector2 MoveInputValue { get; private set; }
        [SerializeField] internal bool GroundDetected;
        [SerializeField] internal bool WallDetected;
        private bool faceToRight = true;
        public float FaceDirection { get; private set; } = 1f;


        private void Awake()
        {
            inputs = new PlayerInputs();
            Assert.IsNotNull(inputs);

            Anim = GetComponentInChildren<Animator>();
            Assert.IsNotNull(Anim);

            rb = GetComponent<Rigidbody2D>();
            Assert.IsNotNull(rb);

            rb.gravityScale = 4f;
            rb.constraints = RigidbodyConstraints2D.FreezeRotation;

            stateMachine = new StateMachine();
            IdleState = new PlayerIdleState(this, stateMachine, "idle");
            MoveState = new PlayerMoveState(this, stateMachine, "move");
            JumpState = new PlayerJumpState(this, stateMachine, "jumpFall");
            FallState = new PlayerFallState(this, stateMachine, "jumpFall");
            WallSlideState = new PlayerWallSlideState(this, stateMachine, "wallSlide");
            WallJumpState = new PlayerWallJumpState(this, stateMachine, "jumpFall");
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

        private void OnDrawGizmos()
        {
            Gizmos.DrawLine(transform.position, transform.position + new Vector3(0, -checkGroundDistance));
            Gizmos.DrawLine(transform.position, transform.position + new Vector3(FaceDirection * checkWallDistance, 0));
        }

        private void Start()
        {
            stateMachine.Init(IdleState);
        }

        private void Update()
        {
            UpdateCollisionDetection();
            stateMachine.currentState.Update();
        }

        public void SetVelocity(float x, float y)
        {
            rb.linearVelocityX = x;
            rb.linearVelocityY = y;
            UpdateDirection(x);
        }

        private void UpdateDirection(float x)
        {
            if ((faceToRight && x < 0) || (!faceToRight && x > 0))
            {
                Flip();
            }
        }

        public void Flip()
        {
            transform.Rotate(0, 180, 0);
            faceToRight = !faceToRight;
            FaceDirection = -FaceDirection;
        }

        private void UpdateCollisionDetection()
        {
            GroundDetected = Physics2D.Raycast(transform.position, Vector2.down, checkGroundDistance, whatIsGround);
            WallDetected = Physics2D.Raycast(transform.position, Vector2.right * FaceDirection, checkWallDistance, whatIsGround);
        }
    }
}
