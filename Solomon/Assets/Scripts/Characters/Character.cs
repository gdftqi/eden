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


        [Header("--------------------- 前方墙体检测 ---------------------")]
        [SerializeField] protected bool switchWallDetected = false;
        [SerializeField] protected bool wallDetected = false;
        [SerializeField] protected float wallCheckWidthScale = 0.9f;
        [SerializeField] protected float wallCheckThickness = 0.12f;
        [SerializeField] protected LayerMask whatIsWall;


        [Header("--------------------- 前方脚下地板检测 ---------------------")]
        [SerializeField] protected bool switchFrontGroundDetected = false;
        [SerializeField] protected bool frontGroundDetected = false;
        [SerializeField] protected float frontGroundCheckThickness = 0.12f;


        [Header("--------------------- 游戏角色属性 --------------------")]
        public CharacterStat stat = new CharacterStat();


        [Header("--------------------- UI 血条 --------------------")]
        [SerializeField] protected float healthBarOffset = 0.5f;


        [Header("--------------------- 朝向 --------------------")]
        [SerializeField] protected float faceDirection = 1f;


        [Header("--------------------- 攻击判定 --------------------")]
        [SerializeField] protected bool switchAttackCheck = false;
        [SerializeField] protected bool attackDetected = false;
        [SerializeField] protected Vector2 attackCheckOriginal;
        [SerializeField] protected float attackCheckWidth;
        [SerializeField] protected float attackCheckHeight;
        [SerializeField] protected LayerMask whatIsEnemy;


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


        /// <summary>
        /// 受击后的表现层回调
        /// </summary>
        public virtual void OnDamaged(DamageInfo info)
        {
            UI_DamageText.Spawn(coll.bounds.center, info);
        }


        protected override void Awake()
        {
            base.Awake();
            InitComponents();
            skeleton.AnimationState.Event += OnAttackEvent;
            stat.ApplyMajorBonus();
            stat.HP.Fill();

            InitUI_CharacterHP();
        }



        protected override void OnDestroy()
        {
            base.OnDestroy();
            skeleton.AnimationState.Event -= OnAttackEvent;
        }


        protected virtual void OnEnable()
        {
            faceDirection = transform.right.x >= 0f ? 1f : -1f;
        }


        protected virtual void OnDisable()
        {

        }


        protected override void Update()
        {
            base.Update();

            if (attackTimer > 0f)
            {
                attackTimer -= Time.deltaTime;
            }
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


        protected void SetFacing(float direction)
        {
            float next = direction >= 0f ? 1f : -1f;

            if (next == faceDirection)
            {
                return;
            }

            faceDirection = next;
            transform.rotation = faceDirection > 0f ? Quaternion.identity : Quaternion.Euler(0f, 180f, 0f);
        }



        private void Reset()
        {
            InitComponents();
        }


        private void InitComponents()
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


        private void InitUI_CharacterHP()
        {
            GameObject prefab = Resources.Load<GameObject>("Prefabs/UI_CharacterHP");

            if (prefab == null)
            {
                Debug.LogErrorFormat("Prefabs/UI_CharacterHP 不在存");
                return;
            }

            GameObject bar = Instantiate(prefab, transform);
            bar.transform.localPosition = new Vector3(coll.offset.x, coll.offset.y + coll.size.y * 0.5f + healthBarOffset, 0f);
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

            if (!Application.isPlaying)
            {
                faceDirection = transform.right.x >= 0f ? 1f : -1f;
            }

            Collider2D col = coll != null ? coll : GetComponent<Collider2D>();

            if (switchAttackCheck)
            {
                Gizmos.color = attackDetected ? Color.green : Color.red;
                Gizmos.DrawWireCube(AttackCheckCenter(), new Vector3(attackCheckWidth, attackCheckHeight, 0f));
            }

            if (switchWallDetected && col != null)
            {
                Gizmos.color = wallDetected ? Color.green : Color.red;
                Gizmos.DrawWireCube(WallCheckOrigin(col), WallCheckSize(col));
            }

            if (switchFrontGroundDetected && col != null)
            {
                Gizmos.color = frontGroundDetected ? Color.green : Color.red;
                Gizmos.DrawWireCube(FrontGroundCheckOrigin(col), FrontGroundCheckSize(col));
            }
        }


        protected Vector2 AttackCheckCenter()
        {
            return (Vector2)transform.position + new Vector2(attackCheckOriginal.x * faceDirection, attackCheckOriginal.y);
        }


        protected override void FixedUpdate()
        {
            base.FixedUpdate();

            if (coll == null)
            {
                return;
            }

            if (switchWallDetected)
            {
                wallDetected = Physics2D.OverlapBox(WallCheckOrigin(coll), WallCheckSize(coll), 0f, whatIsWall) != null;

                if (!wallDetected && switchFrontGroundDetected)
                {
                    frontGroundDetected = Physics2D.OverlapBox(FrontGroundCheckOrigin(coll), FrontGroundCheckSize(coll), 0f, whatIsGround) != null;
                }
            }
        }


        private Vector2 WallCheckOrigin(Collider2D col)
        {
            return new Vector2(col.bounds.center.x + faceDirection * (col.bounds.size.x + wallCheckThickness) * 0.5f, col.bounds.center.y);
        }


        private Vector2 WallCheckSize(Collider2D col)
        {
            return new Vector2(wallCheckThickness, col.bounds.size.y * wallCheckWidthScale);
        }


        private Vector2 FrontGroundCheckOrigin(Collider2D col)
        {
            return new Vector2(col.bounds.center.x + faceDirection * col.bounds.size.x, col.bounds.min.y - frontGroundCheckThickness * 0.5f);
        }


        private Vector2 FrontGroundCheckSize(Collider2D col)
        {
            return new Vector2(col.bounds.size.x, frontGroundCheckThickness);
        }
    }
}
