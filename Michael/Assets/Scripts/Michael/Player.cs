using System;
using UnityEditor;
using UnityEngine;
using UnityEngine.Assertions;
using UnityEngine.InputSystem;


namespace Michael
{
    public class Player : MonoBehaviour
    {
        public PlayerInputs inputs;
        public Rigidbody2D rb;

        private StateMachine stateMachine;
        public Animator Anim { get; private set; }
        public EntityState IdleState { get; private set; }
        public EntityState MoveState { get; private set; }
        public EntityState JumpState { get; private set; }
        public EntityState FallState {  get; private set; }

        private bool faceToRight = true;

        public Vector2 MoveInputValue { get; private set; }
        public float MoveSpeed = 5f;
        public float JumpForce = 5f;

        public float YValue = 0f;


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

        private void Start()
        {
            stateMachine.Init(IdleState);
        }

        private void Update()
        {
            stateMachine.currentState.Update();
            YValue = rb.linearVelocityY;
        }

        public void SetVelocity(float x, float y = 0)
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
    }
}
