using UnityEngine;
using UnityEngine.Assertions;

namespace Michael.Animation
{
    public class EntityAnimationTriggers : MonoBehaviour
    {
        private Entity entity;
        private EntityCombat entityCombat;

        private void Awake()
        {
            entity = GetComponentInParent<Entity>();
            Assert.IsNotNull(entity);

            entityCombat = GetComponentInParent<EntityCombat>();
        }

        public void CurrentStateTrigger()
        {
            entity.CallAnimationTrigger();
        }

        public void AttackTrigger()
        {
            if (entityCombat != null)
            {
                entityCombat.PerformAttack();
            }
        }
    }
}
