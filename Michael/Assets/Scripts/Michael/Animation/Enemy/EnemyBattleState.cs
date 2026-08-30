using UnityEngine;

namespace Michael.Animation
{
    public class EnemyBattleState : EnemyState
    {
        private Transform player;

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
        }

        public override void Update()
        {
            base.Update();

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

        private bool WithinAttackRange()
        {
            return DistanceToPlayer() < enemy.attackDistance;
        }

        private float DistanceToPlayer()
        {
            if (player == null)
            {
                return float.MaxValue;
            }

            return Mathf.Abs(player.position.x - enemy.transform.position.x);
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
