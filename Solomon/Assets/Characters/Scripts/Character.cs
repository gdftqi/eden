using Spine.Unity;
using UnityEngine;

namespace Solomon
{
    [RequireComponent(typeof(Rigidbody2D), typeof(BoxCollider2D), typeof(SkeletonAnimation))]
    public class Character : Actor
    {
        protected Rigidbody2D body;
        protected CapsuleCollider2D capsule;
        protected SkeletonAnimation skeleton;

        private string currentAnim;
        private float attackTimer;   // > 0 表示攻击动画还在播

        [Header("--------------------- 运动 --------------------")]
        [SerializeField] protected StatLocomotion locomotion = new StatLocomotion();

        protected override void Awake()
        {
            base.Awake();
            Init();
            skeleton.AnimationState.Event += OnSpineEvent;
        }


        protected override void OnDestroy()
        {
            base.OnDestroy();
            skeleton.AnimationState.Event -= OnSpineEvent;
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


        protected void ForcePlayAnim(string name, bool loop)
        {
            skeleton.AnimationState.SetAnimation(0, name, loop);
            currentAnim = name;
        }


        public bool IsAttacking()
        {
            return attackTimer > 0f;
        }


        public void Attack()
        {
            if (IsAttacking())
            {
                return;
            }

            Spine.Animation anim = skeleton.Skeleton.Data.FindAnimation("attack");

            if (anim == null)
            {
                return;
            }

            attackTimer = anim.Duration;
            ForcePlayAnim("attack", false);
        }


        protected override void Update()
        {
            base.Update();

            if (attackTimer > 0f)
            {
                attackTimer -= Time.deltaTime;
            }
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
            if (!groundDetected)
            {
                return;
            }

            float t = locomotion.JumpTime();
            float a = 2f * locomotion.JumpHeight.GetValue() / (t * t);
            body.gravityScale = a / Mathf.Abs(Physics2D.gravity.y);
            body.linearVelocityY = 2f * locomotion.JumpHeight.GetValue() / t + a * Time.fixedDeltaTime * 0.5f;
        }


        public void Move(float inputX)
        {
            float target = inputX * locomotion.MoveSpeed.GetValue();
            float current = body.linearVelocityX;

            float rate;

            if (Mathf.Abs(target) < 0.01f)
            {
                rate = locomotion.Deceleration();
            }
            else if (current * target < 0f)
            {
                rate = locomotion.Acceleration() * locomotion.TurnBoost();
            }
            else
            {
                rate = locomotion.Acceleration();
            }

            if (!groundDetected)
            {
                rate *= locomotion.AirControl();
            }

            body.linearVelocityX = Mathf.MoveTowards(current, target, rate * Time.fixedDeltaTime);
        }


        private void OnSpineEvent(Spine.TrackEntry entry, Spine.Event e)
        {
            if (e.Data.Name == "hit")
            {
                Debug.Log("--------------------------- attacked ------------------------------");
            }
        }
    }
}
