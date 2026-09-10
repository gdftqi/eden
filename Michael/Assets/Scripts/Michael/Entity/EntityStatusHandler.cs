using System.Collections;
using UnityEngine;

namespace Michael
{
    public class EntityStatusHandler : MonoBehaviour
    {
        private Entity entity;
        private EntityVFX entityVFX;
        private EntityStats stats;
        private ElementType currentEffect = ElementType.None;

        private void Awake()
        {
            entity = GetComponent<Entity>();
            entityVFX = GetComponent<EntityVFX>();
            stats = GetComponent<EntityStats>();
        }

        public void ApplyChilledEffect(float duration, float slowMultiplier)
        {
            float iceResistance = stats.GetElementalResistance(ElementType.Ice);
            float reducedDuration = duration * (1 - iceResistance);
            
            StartCoroutine(ChilledEffectCo(reducedDuration, slowMultiplier));
        }

        private IEnumerator ChilledEffectCo(float duration, float slowMultiplier)
        {
            entity.SlowDownEntity(duration, slowMultiplier);
            currentEffect = ElementType.Ice;
            entityVFX.PlayOnStatusVFX(duration, currentEffect);
            yield return new WaitForSeconds(duration);  
            currentEffect = ElementType.None;
        }

        public bool CanBeApplied(ElementType element)
        {
            return currentEffect == ElementType.None;
        }
    }
}
