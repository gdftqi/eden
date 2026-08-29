using Michael.Animation;
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
        [SerializeField] private float checkGroundDistance = 1.45f; // 高度检测长度
        [SerializeField] private float checkWallDistance = 0.55f;
        [SerializeField] private float checkWallUpOffset = 0.3f;
        [SerializeField] private float checkWallDownOffset = 1.0f;
        [SerializeField] private LayerMask whatIsGround;



        
        internal bool GroundDetected;
        internal bool WallDetected;
        private bool faceToRight = true;
        public float FaceDirection { get; private set; } = 1f;


        protected virtual void Awake()
        {
            

            Anim = GetComponentInChildren<Animator>();
            Assert.IsNotNull(Anim);

            rb = GetComponent<Rigidbody2D>();
            Assert.IsNotNull(rb);

            rb.gravityScale = 4f;
            rb.constraints = RigidbodyConstraints2D.FreezeRotation;
            rb.interpolation = RigidbodyInterpolation2D.Interpolate;

            stateMachine = new StateMachine();
            
        }

        private Vector3 WallRayUp()
        {
            return new Vector3(transform.position.x, transform.position.y + checkWallUpOffset);
        }


        private Vector3 WallRayDown()
        {
            return new Vector3(transform.position.x, transform.position.y - checkWallDownOffset);
        }

        protected virtual void Start()
        {

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

        public void CallAnimationTrigger()
        {
            stateMachine.currentState.CallAnimationTrigger();
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

            var dir = Vector2.right * FaceDirection;
            WallDetected = Physics2D.Raycast(WallRayUp(), dir, checkWallDistance, whatIsGround) || Physics2D.Raycast(WallRayDown(), dir, checkWallDistance, whatIsGround);
        }

        private void OnDrawGizmos()
        {
            Gizmos.color = Color.green;
            Gizmos.DrawLine(transform.position, transform.position + new Vector3(0, -checkGroundDistance));

            Gizmos.color = Color.cyan;
            var offset = new Vector3(FaceDirection * checkWallDistance, 0);
            Gizmos.DrawLine(WallRayUp(), WallRayUp() + offset);
            Gizmos.DrawLine(WallRayDown(), WallRayDown() + offset);
        }
    }
}
