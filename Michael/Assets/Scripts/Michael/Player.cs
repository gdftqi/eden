using System;
using UnityEditor;
using UnityEngine;
using UnityEngine.Assertions;
using UnityEngine.InputSystem;
using UnityEngine.InputSystem.Utilities;


namespace Michael
{
    public class Player : MonoBehaviour
    {
        [Header("Locomotion properties")]
        public float MoveSpeed = 5f;
        public float JumpForce = 5f;

        [Header("Collision detection")]
        [SerializeField] private float checkHighDistance = 1.35f; // 高度检测长度
        [SerializeField] private LayerMask whatIsGround;
        public bool groundDetected = false;


        internal PlayerInputs inputs;
        internal Rigidbody2D rb {  get; private set; }

        private StateMachine stateMachine;
        internal Animator Anim { get; private set; }
        internal EntityState IdleState { get; private set; }
        internal EntityState MoveState { get; private set; }
        internal EntityState JumpState { get; private set; }
        internal EntityState FallState {  get; private set; }

        
        internal Vector2 MoveInputValue { get; private set; }
        private bool faceToRight = true;


        private void Awake()
        {
            inputs = new PlayerInputs();
            Assert.IsNotNull(inputs);

            Anim = GetComponentInChildren<Animator>();
            Assert.IsNotNull(Anim);

            rb = GetComponent<Rigidbody2D>();
            Assert.IsNotNull(rb);

            stateMachine = new StateMachine();
            IdleState = new PlayerIdleState(this, stateMachine, "idle");
            MoveState = new PlayerMoveState(this, stateMachine, "move");
            JumpState = new PlayerJumpState(this, stateMachine, "jumpFall");
            FallState = new PlayerFallState(this, stateMachine, "jumpFall");
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
            Gizmos.DrawLine(transform.position, transform.position + new Vector3(0, -checkHighDistance));
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
                transform.Rotate(0, 180, 0);
                faceToRight = !faceToRight;
            }
        }

        private void UpdateCollisionDetection()
        {
            groundDetected = Physics2D.Raycast(transform.position, Vector2.down, checkHighDistance, whatIsGround);
        }
    }
}
