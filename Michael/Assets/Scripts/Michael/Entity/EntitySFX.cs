using UnityEngine;

namespace Michael
{
    [RequireComponent(typeof(AudioSource))]
    public class EntitySFX : MonoBehaviour
    {
        [Header("On Damage SFX")]
        [SerializeField] private AudioClip[] onDamageClips;

        [Range(0f, 1f)]
        [SerializeField] private float onDamageVolume = 0.8f;

        [SerializeField] private Vector2 pitchRange = new Vector2(0.92f, 1.08f);

        [Header("On Counter SFX")]
        [SerializeField] private AudioClip[] onCounterClips;

        [Range(0f, 1f)]
        [SerializeField] private float onCounterVolume = 0.9f;

        private AudioSource source;

        private void Awake()
        {
            source = GetComponent<AudioSource>();
            source.playOnAwake = false;
        }

        public void PlayOnDamageSFX()
        {
            if (onDamageClips == null || onDamageClips.Length == 0)
            {
                return;
            }

            var clip = onDamageClips[Random.Range(0, onDamageClips.Length)];

            source.pitch = Random.Range(pitchRange.x, pitchRange.y);
            source.PlayOneShot(clip, onDamageVolume);
        }

        public void PlayOnCounterSFX()
        {
            if (onCounterClips == null || onCounterClips.Length == 0)
            {
                return;
            }

            var clip = onCounterClips[Random.Range(0, onCounterClips.Length)];

            source.pitch = Random.Range(pitchRange.x, pitchRange.y);
            source.PlayOneShot(clip, onCounterVolume);
        }
    }
}
