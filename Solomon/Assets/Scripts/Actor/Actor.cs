using UnityEngine;
using UnityEngine.Assertions;

namespace Solomon
{
    [RequireComponent(typeof(Rigidbody), typeof(CapsuleCollider))]
    public class Actor : MonoBehaviour
    {
        private const float FLIP_THRESHOLD = 0.1f;

        public Rigidbody rb;
        protected CapsuleCollider capsuleCollider;

        protected StateMachine stateMachine;

        [SerializeField, Tooltip("角色朝向: 1 面向右, -1 面向左")]
        public float faceDirection = 1f;

        [SerializeField, Tooltip("初始偏移角度")]
        private float modelYawOffset = 90f;

        [SerializeField, Tooltip("转身角速度, 度/秒. 1440 约等于 0.125 秒转完 180 度")]
        private float turnSpeed = 1440f;

        // 转身的目标朝向. Flip 只改这个值, 实际旋转在 Update 里逐帧靠拢, 避免一帧翻完的生硬感.
        private float targetYaw;

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
            InitRigibody();

            stateMachine = new StateMachine();
        }


        protected virtual void Start()
        {

        }


        protected virtual void Update()
        {
            groundDetected = Physics.Raycast(transform.position, Vector3.down, groundCheckDistance, whatIsGround);
            stateMachine.currentState.Update();

            // 匀速转向目标朝向. 用 RotateTowards 而不是 Slerp: 角速度恒定, 转身耗时可预测.
            rb.rotation = Quaternion.RotateTowards(
                rb.rotation, Quaternion.Euler(0f, targetYaw, 0f), turnSpeed * Time.deltaTime);
        }

        protected virtual void FixedUpdate()
        {
            float vy = rb.linearVelocity.y;
            float t = JumpTime;
            if (vy == 0f || t <= 0f)
            {
                return;
            }

            var jumpForce = locomotion.moveSpeed.GetValue() * 0.416f;
            float target = 2f * jumpForce / (t * t);

            float g = -Physics.gravity.y;
            rb.linearVelocity += Vector3.down * (target - g) * Time.fixedDeltaTime;
        }


        public void SetVelocity(float x, float y)
        {
            var moveSpeed = locomotion.moveSpeed.GetValue();
            rb.linearVelocity = new Vector3(x * moveSpeed, y, rb.linearVelocity.z);
        }


        public void Jump()
        {
            float t = JumpTime;
            if (groundDetected && t > 0f)
            {
                var jumpForce = locomotion.moveSpeed.GetValue() * 0.416f;

                float a = 2f * jumpForce / (t * t);
                float v = 2f * jumpForce / t + a * Time.fixedDeltaTime * 0.5f;
                rb.linearVelocity = new Vector3(rb.linearVelocity.x, v, rb.linearVelocity.z);
            }
        }


        public void Flip()
        {
            faceDirection = -faceDirection;
            targetYaw = (faceDirection > 0f ? 0f : 180f) + modelYawOffset;
        }


        private void InitRigibody()
        {
            if (rb == null)
            {
                rb = GetComponent<Rigidbody>();
            }

            Assert.IsNotNull(rb);
            rb.constraints = RigidbodyConstraints.FreezeRotation | RigidbodyConstraints.FreezePositionZ;
            rb.interpolation = RigidbodyInterpolation.Interpolate;
            rb.collisionDetectionMode = CollisionDetectionMode.Continuous;

            if (capsuleCollider == null)
            {
                capsuleCollider = GetComponent<CapsuleCollider>();
            }

            capsuleCollider.height = 1.65f;
            capsuleCollider.radius = 0.2f;
            capsuleCollider.center = new Vector3(0f, 0.81f, 0f);

            targetYaw = (faceDirection > 0f ? 0f : 180f) + modelYawOffset;
            transform.rotation = Quaternion.Euler(0f, targetYaw, 0f);
        }


        private void Reset()
        {
            InitRigibody();
        }


        private void OnValidate()
        {
            InitRigibody();
        }


        private void OnDrawGizmos()
        {
            Gizmos.color = Color.red;
            Gizmos.DrawLine(transform.position, transform.position + new Vector3(0, -groundCheckDistance));
        }
    }
}
