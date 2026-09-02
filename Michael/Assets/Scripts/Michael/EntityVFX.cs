using System.Collections;
using UnityEngine;
using UnityEngine.Assertions;

namespace Michael
{
    public class EntityVFX : MonoBehaviour
    {
        private SpriteRenderer sr;

        [Header("On Damage VFX")]
        [SerializeField] private Material mOnDamage;
        [SerializeField] private float onDamageVFXDuration = 0.2f;
        private Material mOrignal;
        private Coroutine onDamageVFXCoroutines;

        private void Awake()
        {
            sr = GetComponentInChildren<SpriteRenderer>();
            Assert.IsNotNull(sr);

            mOrignal = sr.material;
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
