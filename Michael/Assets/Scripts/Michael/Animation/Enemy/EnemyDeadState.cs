using UnityEngine;

namespace Michael.Animation
{
    public class EnemyDeadState : EnemyState
    {
        private Collider2D col;

        public EnemyDeadState(Enemy enemy, StateMachine sm, string conditionName) : base(enemy, sm, conditionName)
        {
            col = enemy.GetComponent<Collider2D>();
        }

        public override void Enter()
        {
            col.enabled = anim.enabled = false;
            rb.gravityScale = 12f;
            rb.linearVelocityY = 15f;
        }
    }
}
