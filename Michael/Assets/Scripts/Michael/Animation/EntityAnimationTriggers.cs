using UnityEngine;
using UnityEngine.Assertions;

namespace Michael.Animation
{
    public class EntityAnimationTriggers : MonoBehaviour
    {
        private Entity entity;

        private void Awake()
        {
            entity = GetComponentInParent<Entity>();
            Assert.IsNotNull(entity);
        }

        public void CurrentStateTrigger()
        {
            entity.CallAnimationTrigger();
        }
    }
}
