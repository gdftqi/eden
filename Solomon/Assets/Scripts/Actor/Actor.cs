using UnityEngine;


namespace Solomon
{
    public class Actor : MonoBehaviour
    {
        [Header("--------------------- Ground 检测 ---------------------")]
        [SerializeField] protected bool groundDetected = false;
        [SerializeField] protected bool switchGroundDetected = false;
        [SerializeField] protected float groundDetectedDistance = 1.1f;
        [SerializeField] protected float groundDetectedOriginOffset = 1f;
        [SerializeField] protected LayerMask whatIsGround;

        private float groundIgnoreTimer;


        protected virtual void Awake()
        {
            
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
            if (switchGroundDetected)
            {
                Vector2 origin = (Vector2)transform.position + Vector2.up * groundDetectedOriginOffset;
                Gizmos.color = Color.red;
                Gizmos.DrawLine(origin, origin + new Vector2(0f, -groundDetectedDistance));
            }
        }


        protected void IgnoreGroundFor(float seconds)
        {
            groundIgnoreTimer = seconds;
        }


        private void UpdateGroundDetectedCheck()
        {
            if (groundIgnoreTimer > 0f)
            {
                groundIgnoreTimer -= Time.fixedDeltaTime;
                groundDetected = false;
                return;
            }

            if (switchGroundDetected)
            {
                Vector2 origin = (Vector2)transform.position + Vector2.up * groundDetectedOriginOffset;
                groundDetected = Physics2D.Raycast(origin, Vector2.down, groundDetectedDistance, whatIsGround);
            }
        }
    }
}

