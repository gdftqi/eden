using Spine.Unity;
using System.Collections.Generic;
using UnityEngine;

namespace Solomon
{
    [RequireComponent(typeof(Rigidbody2D), typeof(BoxCollider2D), typeof(SkeletonAnimation))]
    public class Character : Actor
    {
        protected Rigidbody2D body;
        protected BoxCollider2D coll;
        protected SkeletonAnimation skeleton;

        private string currentAnim;
        private float attackTimer;   // > 0 表示攻击动画还在播


        public CharacterStat stat = new CharacterStat();


        [Header("--------------------- 攻击判定 --------------------")]
        [SerializeField] protected bool switchAttackCheck = false;
        [SerializeField] protected bool attackDetected = false;
        [SerializeField] protected Vector2 attackCheckOriginal;
        [SerializeField] protected float attackCheckWidth;
        [SerializeField] protected float attackCheckHeight;
        [SerializeField] protected LayerMask whatIsEnemy;


        protected override void Awake()
        {
            base.Awake();
            Init();
            skeleton.AnimationState.Event += OnAttackEvent;
        }


        protected override void OnDestroy()
        {
            base.OnDestroy();
            skeleton.AnimationState.Event -= OnAttackEvent;
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

            if (coll == null)
            {
                coll = GetComponent<BoxCollider2D>();
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

            float t = stat.Locomotion.JumpTime();
            float a = 2f * stat.Locomotion.JumpHeight.GetValue() / (t * t);
            body.gravityScale = a / Mathf.Abs(Physics2D.gravity.y);
            body.linearVelocityY = 2f * stat.Locomotion.JumpHeight.GetValue() / t + a * Time.fixedDeltaTime * 0.5f;
        }


        public void Move(float inputX)
        {
            float target = inputX * stat.Locomotion.MoveSpeed.GetValue();
            float current = body.linearVelocityX;

            float rate;

            if (Mathf.Abs(target) < 0.01f)
            {
                rate = stat.Locomotion.Deceleration();
            }
            else if (current * target < 0f)
            {
                rate = stat.Locomotion.Acceleration() * stat.Locomotion.TurnBoost();
            }
            else
            {
                rate = stat.Locomotion.Acceleration();
            }

            if (!groundDetected)
            {
                rate *= stat.Locomotion.AirControl();
            }

            body.linearVelocityX = Mathf.MoveTowards(current, target, rate * Time.fixedDeltaTime);
        }


        private void OnAttackEvent(Spine.TrackEntry entry, Spine.Event e)
        {
            if (e.Data.Name != "hit")
            {
                return;
            }

            Collider2D[] hits = Physics2D.OverlapBoxAll(AttackCheckCenter(), new Vector2(attackCheckWidth, attackCheckHeight), 0f, whatIsEnemy);

            attackDetected = hits.Length > 0;

            List<Character> targets = new List<Character>();
            foreach (var target in hits)
            {
                var c = target.GetComponentInParent<Character>();
                if (c != null && !targets.Contains(c))
                {
                    targets.Add(c);
                }
            }

            stat.ApplyDamage(targets);
        }


        protected override void OnDrawGizmos()
        {
            base.OnDrawGizmos();

            if (!switchAttackCheck)
            {
                return;
            }

            Gizmos.color = attackDetected ? Color.green : Color.red;
            Gizmos.DrawWireCube(AttackCheckCenter(), new Vector3(attackCheckWidth, attackCheckHeight, 0f));
        }


        protected Vector2 AttackCheckCenter()
        {
            float facing = skeleton != null && skeleton.Skeleton != null ? Mathf.Sign(skeleton.Skeleton.ScaleX) : 1f;

            return (Vector2)transform.position + new Vector2(attackCheckOriginal.x * facing, attackCheckOriginal.y);
        }
    }
}
