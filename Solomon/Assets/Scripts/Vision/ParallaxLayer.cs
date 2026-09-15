using UnityEngine;

namespace Solomon
{
    [DefaultExecutionOrder(1000)]
    public class ParallaxLayer : MonoBehaviour
    {
        [SerializeField, Range(0f, 1f)]
        private float factor = 0.5f;

        private Transform cam;
        private float startX;
        private float startCamX;

        private void Start()
        {
            cam = Camera.main.transform;
            startX = transform.position.x;
            startCamX = cam.position.x;
        }

        void LateUpdate()
        {
            float camDelta = cam.position.x - startCamX;
            transform.position = new Vector3(startX + camDelta * factor, transform.position.y, transform.position.z);
        }
    }
}
