using Michael.Animation;
using System;
using UnityEngine;

namespace Michael
{
    public class Enemy : Entity
    {
        public EnemyState IdleState { get; set; }
        public EnemyState MoveState { get; set; }
        public EnemyState AttackState { get; set; }
        public EnemyState BattleState { get; set; }
        public EnemyState DeadState { get; set; }
        public EnemyState StunnedState { get; set; }

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
        public float retreatDuration = 0.2f;

        [Header("Player detection")]
        [SerializeField] private LayerMask whatIsPlayer;
        [SerializeField] private Transform playerCheck;
        [SerializeField] internal float playerCheckDistance = 10f;
        public Transform PlayerTransform { get; private set; }

        [Header("Stunned details")]
        public float StunnedDuration = 1f;
        public Vector2 StunnedVelocity = new Vector2(7f, 7f);
        [SerializeField] protected bool CanBeStunned = false;

        public void EnableCounterWindow(bool enabled)
        {
            CanBeStunned = enabled;
        }

        public override void EntityDead()
        {
            base.EntityDead();
            stateMachine.ChangeState(DeadState);
        }

        private void HandlePlayerDeath()
        {
            stateMachine.ChangeState(IdleState);
        }

        public void TryEnterBattleState(Transform player)
        {
            PlayerTransform = player;

            if (stateMachine.currentState == BattleState || stateMachine.currentState == AttackState)
            {
                return;
            }

            UpdateDirection(player.position.x > transform.position.x ? 1f : -1f);

            stateMachine.ChangeState(BattleState);
        }

        public Transform GetPlayerReference()
        {
            if (PlayerTransform == null)
            {
                PlayerTransform = PlayerDetection().transform;
            }

            return PlayerTransform;
        }

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

        private void OnEnable()
        {
            Player.OnPlayerDeath += HandlePlayerDeath;
        }

        private void OnDisable()
        {
            Player.OnPlayerDeath -= HandlePlayerDeath;
        }
    }
}
