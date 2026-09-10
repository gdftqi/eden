using System;
using Unity.VisualScripting;
using UnityEngine;
using UnityEngine.Assertions;

namespace Solomon
{
    [RequireComponent(typeof(Rigidbody))]
    public class Actor : MonoBehaviour
    {
        protected Rigidbody rb;
        [SerializeField] protected float moveSpeed = 5f;
        [SerializeField] protected float faceDirection = 1f;
        [SerializeField] protected float jumpForce = 24f;
        [SerializeField] protected float jumpTime = 0.28f;
        [SerializeField] protected float groundCheckDistance = 1.05f;
        [SerializeField] protected bool groundDetected = true;
        [SerializeField] protected LayerMask whatIsGround;

        protected virtual void Awake()
        {
            rb = GetComponent<Rigidbody>();
            Assert.IsNotNull(rb);

            rb.constraints = RigidbodyConstraints.FreezeRotationZ | RigidbodyConstraints.FreezePositionZ;
        }

        protected virtual void Update()
        {
            groundDetected = Physics.Raycast(transform.position, Vector3.down, groundCheckDistance, whatIsGround);
        }

        protected virtual void FixedUpdate()
        {
            float vy = rb.linearVelocity.y;
            if (vy == 0f || jumpTime <= 0f || jumpTime <= 0f)
            {
                return;
            }

            float target = vy > 0f
                ? jumpForce / jumpTime
                : jumpForce * jumpTime / (jumpTime * jumpTime);

            float g = -Physics.gravity.y;
            rb.linearVelocity += Vector3.down * (target - g) * Time.fixedDeltaTime;
        }


        public void SetVelocity(float x, float y)
        {
            rb.linearVelocity = new Vector3(x * moveSpeed * faceDirection, y, rb.linearVelocity.z);
        }


        public void Jump()
        {
            if (groundDetected)
            {
                rb.linearVelocity = new Vector3(rb.linearVelocity.x, jumpForce, rb.linearVelocity.z);
            }
        }


        private void OnDrawGizmos()
        {
            Gizmos.color = Color.red;
            Gizmos.DrawLine(transform.position, transform.position + new Vector3(0, -groundCheckDistance));
        }
    }
}
