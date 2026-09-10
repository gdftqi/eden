using System.Collections;
using Unity.VisualScripting;
using UnityEngine;
using UnityEngine.Assertions;

namespace Michael
{
    public class EntityVFX : MonoBehaviour
    {
        private SpriteRenderer sr;
        private Entity entity;

        [Header("On Taking Damage VFX")]
        [SerializeField] private Material mOnDamage;
        [SerializeField] private float onDamageVFXDuration = 0.2f;
        private Material orignalMaterial;
        private Coroutine onDamageVFXCoroutines;

        [Header("On Doing Damage VFX")]
        [SerializeField] private Color hitVFXColor = Color.white;
        [SerializeField] private GameObject hitVFX;
        [SerializeField] private GameObject hitCritVFX;

        [Header("Element Colors")]
        [SerializeField] private Color chillVfx = Color.cyan;
        private Color originalHitVfxColor;

        private void Awake()
        {
            sr = GetComponentInChildren<SpriteRenderer>();
            Assert.IsNotNull(sr);
            entity = GetComponent<Entity>();

            orignalMaterial = sr.material;
            originalHitVfxColor = hitVFXColor;
        }

        public void PlayOnStatusVFX(float duration, ElementType element)
        {
            if (element == ElementType.Ice)
            {
                StartCoroutine(PlayStatusVFXCo(duration, chillVfx));
            }
        }

        public IEnumerator PlayStatusVFXCo(float duration, Color color)
        {
            float tickInterval = 0.25f;
            float timeHasPassed = 0f;
            Color lightColor = color * 1.2f;
            Color darkColor = color * 0.8f;
            bool toggle = false;

            while (timeHasPassed < duration)
            {
                sr.color = toggle ? lightColor : darkColor;
                toggle = !toggle;
                yield return new WaitForSeconds(tickInterval);
                timeHasPassed += tickInterval;
            }

            sr.color = Color.white;
        }

        public void CreateOnHitVFX(Transform target, bool isCrit)
        {
            GameObject hitPrefab = isCrit ? hitCritVFX : hitVFX;
            var vfx = Instantiate(hitPrefab, target.position, Quaternion.identity);
            vfx.GetComponentInChildren<SpriteRenderer>().color = hitVFXColor;

            if (entity.FaceDirection == -1f && isCrit)
            {
                vfx.transform.Rotate(0f, 180f, 0f);
            }
        }

        public void UpdateOnHitColor(ElementType element)
        {
            if (element == ElementType.Ice)
            {
                hitVFXColor = chillVfx;
            }

            if (element == ElementType.None)
            {
                hitVFXColor = originalHitVfxColor;
            }
        }

        public void PlayOnDamageVFX()
        {
            if (onDamageVFXCoroutines != null)
            {
                StopCoroutine(onDamageVFXCoroutines);
            }

            onDamageVFXCoroutines = StartCoroutine(OnDamageVFXCo());
        }

        private IEnumerator OnDamageVFXCo()
        {
            sr.material = mOnDamage;
            yield return new WaitForSeconds(onDamageVFXDuration);
            sr.material = orignalMaterial;
        }
    }
}
