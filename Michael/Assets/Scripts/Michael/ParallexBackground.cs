using UnityEngine;
using UnityEngine.Assertions;


namespace Michael
{
    [DefaultExecutionOrder(100)]
    public class ParallexBackground : MonoBehaviour
    {
        private Camera mainCamera;
        private float lastCameraPositionX = 0f;
        private float cameraHalfWidth = 0f;

        [SerializeField] private ParallexLayer[] backgroundLayers;

        private void Awake()
        {
            mainCamera = Camera.main;
            Assert.IsNotNull(mainCamera);

            cameraHalfWidth = mainCamera.orthographicSize * mainCamera.aspect;

            CaculateImageLength();
        }

        private void Update()
        {
            float currentCameraPositionX = mainCamera.transform.position.x;
            float distanceToMove = currentCameraPositionX - lastCameraPositionX;
            lastCameraPositionX = currentCameraPositionX;

            float cameraLeftEdge = currentCameraPositionX - cameraHalfWidth;
            float cameraRightEdge = currentCameraPositionX + cameraHalfWidth;

            foreach (var layer in backgroundLayers)
            {
                layer.Move(distanceToMove);
                layer.LoopBackground(cameraLeftEdge, cameraRightEdge);
            }
        }

        private void CaculateImageLength()
        {
            foreach (var layer in backgroundLayers)
            {
                layer.CaculateImageWidth();
            }
        }
    }
}
