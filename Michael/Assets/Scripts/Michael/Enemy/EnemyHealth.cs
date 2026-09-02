using UnityEngine;

namespace Michael
{
    public class EnemyHealth : EntityHealth
    {
        private Enemy enemy;

        private void Start()
        {
            enemy = GetComponent<Enemy>();
        }

        public override void TakeDamage(float damage, Transform damageDealer)
        {
            base.TakeDamage(damage, damageDealer);
            if (damageDealer.GetComponent<Player>() != null)
            {
                enemy.TryEnterBattleState(damageDealer);
            }
        }
    }
}
