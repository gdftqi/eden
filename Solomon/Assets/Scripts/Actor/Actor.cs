using UnityEngine;
using UnityEngine.Assertions;

namespace Solomon
{
    [RequireComponent(typeof(Rigidbody))]
    public class Actor : MonoBehaviour
    {
        private const float FLIP_THRESHOLD = 0.1f;

        protected Rigidbody rb;

        [SerializeField, Tooltip("角色朝向: 1 面向右, -1 面向左")]
        protected float faceDirection = 1f;
    
        [SerializeField, Tooltip("地面检测距离")]
        protected float groundCheckDistance = 1.05f;

        [SerializeField, Tooltip("是否检测到地面")]
        protected bool groundDetected = true;

        [SerializeField, Tooltip("地面图层")]
        protected LayerMask whatIsGround;

        [SerializeField, Tooltip("运动属性")]
        StatLocomotionGroup locomotion = new StatLocomotionGroup(5f);

        [SerializeField, Tooltip("主属性")]
        StatMajorGroup major;

        [SerializeField, Tooltip("攻击类属性")]
        StatOffenseGroup offense;

        [SerializeField, Tooltip("防御类属性")]
        StatDefenseGroup defense;

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
        }


        protected virtual void Update()
        {
            groundDetected = Physics.Raycast(transform.position, Vector3.down, groundCheckDistance, whatIsGround);

            float vx = rb.linearVelocity.x;
            if (Mathf.Abs(vx) >= FLIP_THRESHOLD && Mathf.Sign(vx) != faceDirection)
            {
                Flip();
            }
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
            transform.Rotate(0f, 180f, 0f);
            faceDirection = -faceDirection;
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
        }


        private void InitCamera()
        {

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
