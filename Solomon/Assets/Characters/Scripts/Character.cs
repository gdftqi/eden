using Spine.Unity;
using UnityEngine;

namespace Solomon
{
    [RequireComponent(typeof(Rigidbody2D), typeof(BoxCollider2D), typeof(SkeletonAnimation))]
    public class Character : Actor
    {
        [Header("--------------------- 移动 ---------------------")]
        public float moveSpeed = 6f;

        [Header("--------------------- 跳跃 ---------------------")]
        [SerializeField] protected float jumpHeight = 3f;    // 最高点高度, 单位: 米
        [SerializeField] protected float jumpSpan = 8f;     // 一次跳跃的水平距离, 单位: 米

        [SerializeField] protected float jumpTimeMin = 0.15f;   // 上升时间下限, 单位: 秒
        [SerializeField] protected float jumpTimeMax = 0.35f;   // 上升时间上限, 单位: 秒

        [Header("--------------------- 手感 ---------------------")]
        [Range(0f, 1f)]
        [SerializeField] protected float responsiveness = 0.7f;


        private const float AccelTimeSlow = 0.35f;
        private const float AccelTimeFast = 0.085f;
        private const float DecelTimeSlow = 0.42f;
        private const float DecelTimeFast = 0.10f;
        private const float TurnBoostSlow = 1.0f;
        private const float TurnBoostFast = 3.0f;
        private const float AirControlSlow = 0.3f;
        private const float AirControlFast = 0.95f;


        private float LerpTime(float slow, float fast)
        {
            return slow * Mathf.Pow(fast / slow, responsiveness);
        }


        protected float Acceleration()
        {
            return moveSpeed / LerpTime(AccelTimeSlow, AccelTimeFast);
        }


        protected float Deceleration()
        {
            return moveSpeed / LerpTime(DecelTimeSlow, DecelTimeFast);
        }


        protected float TurnBoost()
        {
            return TurnBoostSlow * Mathf.Pow(TurnBoostFast / TurnBoostSlow, responsiveness);
        }


        protected float AirControl()
        {
            return AirControlSlow * Mathf.Pow(AirControlFast / AirControlSlow, responsiveness);
        }


        protected float JumpTime()
        {
            return Mathf.Clamp(jumpSpan / (2f * Mathf.Max(moveSpeed, 0.01f)), jumpTimeMin, jumpTimeMax);
        }

        protected Rigidbody2D body;
        protected CapsuleCollider2D capsule;
        protected SkeletonAnimation skeleton;


        private string currentAnim;
        private float attackTimer;   // > 0 表示攻击动画还在播


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


        // 攻击这类一次性动画不能走 PlayAnim 的去重: 播完之后 Spine 停在最后一帧,
        // 但 currentAnim 还记着这个名字, 第二次按攻击会被去重挡掉放不出来.
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

            // 时长从 Spine 数据里取, 不写死. 美术改了动画长度代码自动跟上.
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

            float t = JumpTime();
            float a = 2f * jumpHeight / (t * t);
            body.gravityScale = a / Mathf.Abs(Physics2D.gravity.y);
            body.linearVelocityY = 2f * jumpHeight / t + a * Time.fixedDeltaTime * 0.5f;
        }


        public void Move(float inputX)
        {
            float target = inputX * moveSpeed;
            float current = body.linearVelocityX;

            float rate;

            if (Mathf.Abs(target) < 0.01f)
            {
                rate = Deceleration();
            }
            else if (current * target < 0f)
            {
                rate = Acceleration() * TurnBoost();
            }
            else
            {
                rate = Acceleration();
            }

            if (!groundDetected)
            {
                rate *= AirControl();
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
