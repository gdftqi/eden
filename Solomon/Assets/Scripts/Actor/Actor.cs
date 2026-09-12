using UnityEngine;
using UnityEngine.Assertions;

namespace Solomon
{
    [RequireComponent(typeof(CharacterController))]
    public class Actor : MonoBehaviour
    {
        private const float GROUND_STICK_SPEED = -2f;

        protected CharacterController cc;
        public Vector3 velocity;

        protected StateMachine stateMachine;

        [SerializeField, Tooltip("角色朝向: 1 面向右, -1 面向左")]
        public float faceDirection = 1f;

        [SerializeField, Tooltip("初始偏移角度")]
        private float modelYawOffset = 90f;

        [SerializeField, Tooltip("转身角速度, 度/秒. 1440 约等于 0.125 秒转完 180 度")]
        private float turnSpeed = 1440f;

        private float targetYaw;

        public bool animationDrivenRotation;

        [SerializeField, Tooltip("地面检测距离")]
        protected float groundCheckDistance = 1.05f;

        [SerializeField, Tooltip("地面图层")]
        protected LayerMask whatIsGround;

        [SerializeField, Tooltip("是否检测到地面")]
        public bool groundDetected = true;

        public bool wallDetected = false;

        [SerializeField, Tooltip("运动属性")]
        public StatLocomotionGroup locomotion = new StatLocomotionGroup(5f);

        [SerializeField, Tooltip("主属性")]
        public StatMajorGroup major;

        [SerializeField, Tooltip("攻击类属性")]
        public StatOffenseGroup offense;

        [SerializeField, Tooltip("防御类属性")]
        public StatDefenseGroup defense;

        protected float JumpTime
        {
            get
            {
                var moveSpeed = locomotion.moveSpeed.GetValue();
                var jumpForce = moveSpeed * 0.416f;

                return (moveSpeed <= 0f) ? 0f : jumpForce / (2f * moveSpeed);
            }
        }
        

        protected virtual void Awake()
        {
            InitController();
            targetYaw = (faceDirection > 0f ? 0f : 180f) + modelYawOffset;
            transform.rotation = Quaternion.Euler(0f, targetYaw, 0f);

            stateMachine = new StateMachine();
        }


        protected virtual void Start()
        {

        }


        protected virtual void Update()
        {
            stateMachine.currentState.Update();

            ApplyGravity();

            Vector3 move = velocity * Time.deltaTime;
            move.z = 0f;
            cc.Move(move);

            groundDetected = cc.isGrounded;
            if (!animationDrivenRotation)
            {
                transform.rotation = Quaternion.RotateTowards(
                    transform.rotation, Quaternion.Euler(0f, targetYaw, 0f), turnSpeed * Time.deltaTime);
            }
        }


        private void ApplyGravity()
        {
            if (groundDetected && velocity.y < 0f)
            {
                velocity.y = GROUND_STICK_SPEED;
                return;
            }

            float t = JumpTime;
            if (t <= 0f)
            {
                return;
            }

            float jumpHeight = locomotion.moveSpeed.GetValue() * 0.416f;
            velocity.y -= 2f * jumpHeight / (t * t) * Time.deltaTime;
        }


        public void SetVelocity(float x, float y)
        {
            velocity.x = x * locomotion.moveSpeed.GetValue();
            velocity.y = y;
        }


        public void Jump()
        {
            float t = JumpTime;
            if (groundDetected && t > 0f)
            {
                var jumpForce = locomotion.moveSpeed.GetValue() * 0.416f;
                float a = 2f * jumpForce / (t * t);
                velocity.y = 2f * jumpForce / t + a * Time.deltaTime * 0.5f;
            }
        }


        public void CallAnimationTrigger()
        {
            stateMachine.currentState.CallAnimationTrigger();
        }


        public void Flip()
        {
            faceDirection = -faceDirection;
            targetYaw = (faceDirection > 0f ? 0f : 180f) + modelYawOffset;
        }


        private void InitController()
        {
            if (cc == null)
            {
                cc = GetComponent<CharacterController>();
            }
            Assert.IsNotNull(cc);
        }


        private void Reset()
        {
            InitController();

            cc.height = 1.65f;
            cc.radius = 0.2f;
            cc.center = new Vector3(0f, 0.81f, 0f);

            targetYaw = (faceDirection > 0f ? 0f : 180f) + modelYawOffset;
            transform.rotation = Quaternion.Euler(0f, targetYaw, 0f);
        }


        private void OnValidate()
        {
            InitController();
        }


        private void OnDrawGizmos()
        {
            Gizmos.color = Color.red;
            Gizmos.DrawLine(transform.position, transform.position + new Vector3(0, -groundCheckDistance));
        }
    }
}
