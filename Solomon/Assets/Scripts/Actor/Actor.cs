using UnityEngine;


namespace Solomon
{
    public class Actor : MonoBehaviour
    {
        [Header("--------------------- Ground 检测 ---------------------")]
        [SerializeField] protected bool groundDetected = false;
        [SerializeField] protected bool switchGroundDetected = false;
        [SerializeField] protected float groundCheckWidthScale = 0.9f;   // 探测盒宽度 = 碰撞体宽 * 这个值
        [SerializeField] protected float groundCheckThickness = 0.12f;   // 探测盒高度
        [SerializeField] protected LayerMask whatIsGround;

        private Collider2D groundCheckCollider;   // 用基类, 换 Box 还是胶囊都不影响
        private float groundIgnoreTimer;


        protected virtual void Awake()
        {
            groundCheckCollider = GetComponent<Collider2D>();
        }


        protected virtual void Update()
        {
            
        }


        protected virtual void FixedUpdate()
        {
            UpdateGroundDetectedCheck();
        }


        protected virtual void OnDestroy()
        {
            
        }


        private void OnDrawGizmos()
        {
            if (!switchGroundDetected)
            {
                return;
            }

            Collider2D col = groundCheckCollider != null ? groundCheckCollider : GetComponent<Collider2D>();

            if (col == null)
            {
                return;
            }

            Gizmos.color = groundDetected ? Color.green : Color.red;
            Gizmos.DrawWireCube(GroundCheckOrigin(col), GroundCheckSize(col));
        }


        protected void IgnoreGroundFor(float seconds)
        {
            groundIgnoreTimer = seconds;
        }


        private Vector2 GroundCheckOrigin(Collider2D col)
        {
            return new Vector2(col.bounds.center.x, col.bounds.min.y - groundCheckThickness * 0.5f);
        }


        private Vector2 GroundCheckSize(Collider2D col)
        {
            return new Vector2(col.bounds.size.x * groundCheckWidthScale, groundCheckThickness);
        }


        private void UpdateGroundDetectedCheck()
        {
            if (groundIgnoreTimer > 0f)
            {
                groundIgnoreTimer -= Time.fixedDeltaTime;
                groundDetected = false;
                return;
            }

            if (switchGroundDetected && groundCheckCollider != null)
            {
                groundDetected = Physics2D.OverlapBox(
                    GroundCheckOrigin(groundCheckCollider),
                    GroundCheckSize(groundCheckCollider),
                    0f,
                    whatIsGround) != null;
            }
        }
    }
}
