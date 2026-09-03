using Michael.Animation;
using System.Collections;
using UnityEngine;
using UnityEngine.Assertions;


namespace Michael
{
    public class Entity : MonoBehaviour
    {
        internal Rigidbody2D rb { get; private set; }
        protected StateMachine stateMachine;
        internal Animator Anim { get; private set; }

        [Header("Collision detection")]
        [SerializeField] protected LayerMask whatIsGround;
        [SerializeField] private Transform groundCheckPostion;
        [SerializeField] private float groundCheckDistance = 1.45f; // 高度检测长度
        [SerializeField] private float wallCheckDistance = 0.55f;
        [SerializeField] private float wallCheckUpOffset = 0.3f;
        [SerializeField] private float wallCheckDownOffset = 1.0f;
        
        internal bool GroundDetected = false;
        internal bool WallDetected = false;

        public float FaceDirection { get; private set; } = 1f;

        internal bool IsKnocked = false;
        internal Coroutine KnockbackCoroutine;


        protected virtual void Awake()
        {
            Anim = GetComponentInChildren<Animator>();
            Assert.IsNotNull(Anim);

            rb = GetComponent<Rigidbody2D>();
            Assert.IsNotNull(rb);

            rb.gravityScale = 4f;
            rb.constraints = RigidbodyConstraints2D.FreezeRotation;
            rb.interpolation = RigidbodyInterpolation2D.Interpolate;

            FaceDirection = transform.right.x > 0f ? 1f : -1f;

            stateMachine = new StateMachine();
            
        }

        private Vector3 WallRayUp()
        {
            return new Vector3(transform.position.x, transform.position.y + wallCheckUpOffset);
        }


        private Vector3 WallRayDown()
        {
            return new Vector3(transform.position.x, transform.position.y - wallCheckDownOffset);
        }

        protected virtual void Start()
        {

        }

        protected virtual void Update()
        {
            UpdateCollisionDetection();
            stateMachine.currentState.Update();
        }

        public virtual void EntityDead()
        {

        }

        public void ReceiveKnockback(Vector2 knockback, float duration)
        {
            if (KnockbackCoroutine != null)
            {
                StopCoroutine(KnockbackCoroutine);
            }
            KnockbackCoroutine = StartCoroutine(KnockbackCo(knockback, duration));
        }

        private IEnumerator KnockbackCo(Vector2 knockback, float duration)
        {
            IsKnocked = true;
            rb.linearVelocity = knockback;
            yield return new WaitForSeconds(duration);

            rb.linearVelocity = Vector2.zero;
            IsKnocked = false;
        }

        public void SetVelocity(float x, float y)
        {
            if (IsKnocked)
            {
                return;
            }

            rb.linearVelocityX = x;
            rb.linearVelocityY = y;
            UpdateDirection(x);
        }

        public void CallAnimationTrigger()
        {
            stateMachine.currentState.CallAnimationTrigger();
        }

        public void UpdateDirection(float x)
        {
            if (x != 0 && Mathf.Sign(x) != FaceDirection)
            {
                Flip();
            }
        }

        public void Flip()
        {
            transform.Rotate(0, 180, 0);
            FaceDirection = -FaceDirection;
        }

        private void UpdateCollisionDetection()
        {
            GroundDetected = Physics2D.Raycast(groundCheckPostion.position, Vector2.down, groundCheckDistance, whatIsGround);

            var dir = Vector2.right * FaceDirection;
            WallDetected = Physics2D.Raycast(WallRayUp(), dir, wallCheckDistance, whatIsGround) || Physics2D.Raycast(WallRayDown(), dir, wallCheckDistance, whatIsGround);
        }

        protected virtual void OnDrawGizmos()
        {
            Gizmos.color = Color.green;
            Gizmos.DrawLine(groundCheckPostion.position, groundCheckPostion.position + new Vector3(0, -groundCheckDistance));

            Gizmos.color = Color.cyan;
            var offset = new Vector3(FaceDirection * wallCheckDistance, 0);
            Gizmos.DrawLine(WallRayUp(), WallRayUp() + offset);
            Gizmos.DrawLine(WallRayDown(), WallRayDown() + offset);
        }
    }
}
