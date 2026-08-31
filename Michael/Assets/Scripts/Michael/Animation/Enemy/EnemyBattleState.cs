using UnityEngine;

namespace Michael.Animation
{
    public class EnemyBattleState : EnemyState
    {
        private Transform player;
        public float lastTimeWasInBattle = 0f;
        public float inGameTime = 0f;

        public EnemyBattleState(Enemy enemy, StateMachine sm, string conditionName) : base(enemy, sm, conditionName)
        {
        }

        public override void Enter()
        {
            base.Enter();

            if (player == null)
            {
                player = enemy.PlayerDetection().transform;
            }

            if (ShouldRetreat())
            {
                var dir = DirectionToPlayer();
                rb.linearVelocity = new Vector2(enemy.retreatVelocity.x * -dir, enemy.retreatVelocity.y);
                enemy.UpdateDirection(dir);
            }
        }

        public override void Update()
        {
            base.Update();

            if (enemy.PlayerDetection())
            {
                UpdateBattleTimer();
            }

            if (BattleTimeIsOver())
            {
                stateMachine.ChangeState(enemy.IdleState);
                return;
            }

            if (DistanceToPlayer() > enemy.playerCheckDistance)
            {
                stateMachine.ChangeState(enemy.IdleState);
                return;
            }

            if (WithinAttackRange())
            {
                if (DirectionToPlayer() != enemy.FaceDirection)
                {
                    enemy.Flip();
                }

                stateMachine.ChangeState(enemy.AttackState);
                return;
            }

            enemy.SetVelocity(enemy.battleMoveSpeed * DirectionToPlayer(), rb.linearVelocityY);
        }

        private void UpdateBattleTimer()
        {
            lastTimeWasInBattle = Time.time;
        }

        private bool BattleTimeIsOver()
        {
            return Time.time - lastTimeWasInBattle > enemy.battleTimeDuration;
        }

        private bool WithinAttackRange()
        {
            if (player == null)
            {
                return false;
            }

            var delta = player.position - enemy.transform.position;
            return Mathf.Abs(delta.x) < enemy.attackDistance && Mathf.Abs(delta.y) < enemy.attackHeightTolerance;
        }

        private bool ShouldRetreat()
        {
            return DistanceToPlayer() < enemy.minRetreatDistance; 
        }

        private float DistanceToPlayer()
        {
            return player == null ? float.MaxValue : Vector2.Distance(player.position, enemy.transform.position);
        }

        private float DirectionToPlayer()
        {
            if (player == null)
            {
                return 0f;
            }

            return player.position.x > enemy.transform.position.x ? 1f : -1f;
        }
    }
}
