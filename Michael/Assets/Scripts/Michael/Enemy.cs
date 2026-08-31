using System;
using UnityEngine;

namespace Michael
{
    public class Enemy : Entity
    {
        public EntityState IdleState { get; set; }
        public EntityState MoveState { get; set; }
        public EntityState AttackState { get; set; }
        public EntityState BattleState { get; set; }

        [Header("Locomotion properties")]
        public float idleTime = 2;
        public float moveSpeed = 1.5f;
        [Range(0f, 2f)]
        public float moveAnimSpeedMultiplier = 1f;
        public float battleMoveSpeed = 3f;
        public float attackDistance = 2f;
        public float attackHeightTolerance = 1.5f;
        public float battleTimeDuration = 5f;
        public float minRetreatDistance = 1f;
        public Vector2 retreatVelocity = new Vector2(5f, 3f);
        // 回撤的持续时间. 必须够跨过几个物理步(50Hz = 每 20ms 一次), 否则
        // 冲量还没被刚体用上就被下一帧的攻击判定清掉了
        public float retreatDuration = 0.2f;

        [Header("Player detection")]
        [SerializeField] private LayerMask whatIsPlayer;
        [SerializeField] private Transform playerCheck;
        [SerializeField] internal float playerCheckDistance = 10f;

        public RaycastHit2D PlayerDetection()
        {
            var hit = Physics2D.Raycast(playerCheck.position, Vector2.right * FaceDirection, playerCheckDistance, whatIsPlayer | whatIsGround);
            return hit.collider == null || hit.collider.gameObject.layer != LayerMask.NameToLayer("Player") ? default : hit;
        }

        protected override void OnDrawGizmos()
        {
            base.OnDrawGizmos();
            Gizmos.color = Color.yellow;
            Gizmos.DrawLine(playerCheck.position, new Vector3(playerCheck.position.x + playerCheckDistance * FaceDirection, playerCheck.position.y));
            Gizmos.color = Color.red;
            Gizmos.DrawLine(playerCheck.position, new Vector3(playerCheck.position.x + attackDistance * FaceDirection, playerCheck.position.y));
        }

        protected override void Update()
        {
            base.Update();
        }
    }
}
