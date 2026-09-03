using UnityEngine;

namespace Michael.Animation
{
    public class EntityAnimationTriggers : MonoBehaviour
    {
        private Entity entity;
        private EntityCombat entityCombat;

        protected virtual void Awake()
        {
            entity = GetComponentInParent<Entity>();
            entityCombat = GetComponentInParent<EntityCombat>();
        }

        public void CurrentStateTrigger()
        {
            entity?.CallAnimationTrigger();
        }

        public void AttackTrigger()
        {
            entityCombat?.PerformAttack();
        }
    }
}
