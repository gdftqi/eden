using Spine.Unity;
using UnityEngine;

namespace Solomon
{
    [RequireComponent(typeof(Rigidbody2D), typeof(CapsuleCollider2D), typeof(SkeletonAnimation))]
    public class Character : Actor
    {
        public float moveSpeed = 1f;
        protected Rigidbody2D body;
        protected CapsuleCollider2D capsule;
        protected SkeletonAnimation skeleton;


        private string currentAnim;


        protected virtual void Awake()
        {
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
    }
}
