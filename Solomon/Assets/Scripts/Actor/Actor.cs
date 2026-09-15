using UnityEngine;


namespace Solomon
{
    public class Actor : MonoBehaviour
    {
        [Header("--------------------- Ground 检测 ---------------------")]
        [SerializeField] protected bool groundDetected = false;
        [SerializeField] protected bool switchGroundDetected = false;
        [SerializeField] protected float groundDetectedDistance = 0.15f;
        [SerializeField] protected LayerMask whatIsGround;


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
                Gizmos.color = Color.red;
                Gizmos.DrawLine(transform.position, transform.position + new Vector3(0f, -groundDetectedDistance));
            }
        }


        private void UpdateGroundDetectedCheck()
        {
            if (switchGroundDetected)
            {
                groundDetected = Physics2D.Raycast(transform.position, Vector2.down, groundDetectedDistance, whatIsGround);
            }
        }
    }
}

