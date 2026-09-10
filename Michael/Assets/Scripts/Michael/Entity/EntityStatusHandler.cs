using System.Collections;
using UnityEngine;

namespace Michael
{
    public class EntityStatusHandler : MonoBehaviour
    {
        private Entity entity;
        private EntityVFX entityVFX;
        private EntityStats entityStats;
        private EntityHealth entityHealth;
        private ElementType currentEffect = ElementType.None;

        private void Awake()
        {
            entity = GetComponent<Entity>();
            entityVFX = GetComponent<EntityVFX>();
            entityStats = GetComponent<EntityStats>();
            entityHealth = GetComponent<EntityHealth>();
        }

        public void ApplyBurnEffect(float duration, float fireDamage)
        {
            float fireResistance = entityStats.GetElementalResistance(ElementType.Fire);
            float totalDamage = fireDamage * (1 -  fireResistance);
            StartCoroutine(BurnEffectCo(duration, totalDamage));
        }

        private IEnumerator BurnEffectCo(float duration, float totalDamage)
        {
            currentEffect = ElementType.Fire;
            entityVFX.PlayOnStatusVFX(duration, ElementType.Fire);

            var tickersPerSecond = 2f;
            int tickCount = Mathf.RoundToInt(tickersPerSecond * duration);
            float damagePerTick = totalDamage / tickCount;
            float tickInterval = 1f / tickersPerSecond;

            for (int i = 0; i < tickCount; i++)
            {
                entityHealth.ReduceHP(damagePerTick);
                yield return new WaitForSeconds(tickInterval);
            }

            currentEffect = ElementType.None;
        }

        public void ApplyChilledEffect(float duration, float slowMultiplier)
        {
            float iceResistance = entityStats.GetElementalResistance(ElementType.Ice);
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
