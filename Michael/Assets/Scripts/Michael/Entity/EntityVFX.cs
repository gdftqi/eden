using System;
using System.Collections;
using UnityEngine;
using UnityEngine.Assertions;

namespace Michael
{
    public class EntityVFX : MonoBehaviour
    {
        private SpriteRenderer sr;

        [Header("On Taking Damage VFX")]
        [SerializeField] private Material mOnDamage;
        [SerializeField] private float onDamageVFXDuration = 0.2f;
        private Material mOrignal;
        private Coroutine onDamageVFXCoroutines;

        [Header("On Doing Damage VFX")]
        [SerializeField] private Color hitVFXColor = Color.white;
        [SerializeField] private GameObject hitVFX;
        [SerializeField] private GameObject hitCritVFX;

        private void Awake()
        {
            sr = GetComponentInChildren<SpriteRenderer>();
            Assert.IsNotNull(sr);

            mOrignal = sr.material;
        }

        public void CreateOnHitVFX(Transform target, bool isCrit)
        {
            GameObject hitPrefab = isCrit ? hitCritVFX : hitVFX;
            var vfx = Instantiate(hitPrefab, target.position, Quaternion.identity);
            vfx.GetComponentInChildren<SpriteRenderer>().color = hitVFXColor;
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
            sr.material = mOrignal;
        }
    }
}
