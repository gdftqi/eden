using Spine.Unity;
using UnityEngine;

namespace Solomon
{
    [RequireComponent(typeof(Rigidbody2D), typeof(CapsuleCollider2D), typeof(SkeletonAnimation))]
    public class Character : Actor
    {
        [Header("--------------------- 移动 ---------------------")]
        public float moveSpeed = 1f;

        [Header("--------------------- 跳跃 ---------------------")]
        [SerializeField] protected float jumpHeight = 3f;    // 最高点高度, 单位: 米
        [SerializeField] protected float jumpSpan = 10f;     // 一次跳跃的水平距离, 单位: 米

        [SerializeField] protected float jumpTimeMin = 0.15f;   // 上升时间下限, 单位: 秒
        [SerializeField] protected float jumpTimeMax = 0.35f;   // 上升时间上限, 单位: 秒


        protected float JumpTime()
        {
            return Mathf.Clamp(jumpSpan / (2f * Mathf.Max(moveSpeed, 0.01f)), jumpTimeMin, jumpTimeMax);
        }

        protected Rigidbody2D body;
        protected CapsuleCollider2D capsule;
        protected SkeletonAnimation skeleton;


        private string currentAnim;


        protected override void Awake()
        {
            base.Awake();
            Init();
        }


        protected void PlayAnim(string name, bool loop)
        {
            if (name == currentAnim)
            {
                return;
            }

            skeleton.AnimationState.SetAnimation(0, name, loop);
            currentAnim = name;
        }


        protected void SetFacing(bool right)
        {
            skeleton.Skeleton.ScaleX = right ? 1f : -1f;
        }


        private void Reset()
        {
            Init();
        }


        private void Init()
        {
            if (body == null)
            {
                body = GetComponent<Rigidbody2D>();
            }

            if (capsule == null)
            {
                capsule = GetComponent<CapsuleCollider2D>();
            }

            if (skeleton == null)
            {
                skeleton = GetComponent<SkeletonAnimation>();
            }

            body.constraints |= RigidbodyConstraints2D.FreezeRotation;
        }


        public void Jump()
        {
            if (groundDetected)
            {
                float t = JumpTime();
                float a = 2f * jumpHeight / (t * t);
                body.gravityScale = a / Mathf.Abs(Physics2D.gravity.y);
                body.linearVelocityY = 2f * jumpHeight / t + a * Time.fixedDeltaTime * 0.5f;
            }
        }
    }
}
