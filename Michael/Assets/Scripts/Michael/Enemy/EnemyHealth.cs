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

        public override bool TakeDamage(float damage, float elementalDamage, ElementType element, Transform damageDealer)
        {
            if (!base.TakeDamage(damage, elementalDamage, element, damageDealer))
            {
                return false;
            }

            if (damageDealer.GetComponent<Player>() != null)
            {
                enemy.TryEnterBattleState(damageDealer);
            }

            return true;
        }
    }
}
