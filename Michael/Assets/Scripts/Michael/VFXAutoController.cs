using Unity.VisualScripting;
using UnityEngine;

namespace Michael
{
    public class VFXAutoController : MonoBehaviour
    {
        [SerializeField] private bool autoDestory = true;
        [SerializeField] private float destroyDelay = 1f;
        [SerializeField] private bool randomOffset = true;

        [Header("Random Position")]
        [SerializeField] private float xMinOffset = -0.3f;
        [SerializeField] private float xMaxOffset = 0.3f;

        [Space]
        [SerializeField] private float yMinOffset = -0.3f;
        [SerializeField] private float yMaxOffset = 0.3f;

        private void Start()
        {
            ApplyRandomOffset();
            ApplyRandomRotation();

            if (autoDestory)
            {
                Destroy(gameObject, destroyDelay);
            }
        }

        private void ApplyRandomOffset()
        {
            if (!randomOffset)
            {
                return;
            }

            var x = Random.Range(xMinOffset, xMaxOffset);
            var y = Random.Range(yMinOffset, yMaxOffset);
            transform.position = transform.position + new Vector3(x, y, 0);
        }

        private void ApplyRandomRotation()
        {
            if (!randomOffset)
            {
                return;
            }

            float z = Random.Range(0f, 360f);
            transform.Rotate(0f, 0f, z);
        }
    }
}
