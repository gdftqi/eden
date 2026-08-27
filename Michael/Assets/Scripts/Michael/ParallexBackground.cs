using UnityEngine;
using UnityEngine.Assertions;


namespace Michael
{
    [DefaultExecutionOrder(100)]
    public class ParallexBackground : MonoBehaviour
    {
        private Camera mainCamera;
        private float lastCameraPositionX = 0f;

        [SerializeField] private ParallexLayer[] backgroundLayers;

        private void Awake()
        {
            mainCamera = Camera.main;
            Assert.IsNotNull(mainCamera);
        }

        private void Update()
        {
            float currentCameraPositionX = mainCamera.transform.position.x;
            float distanceToMove = currentCameraPositionX - lastCameraPositionX;
            lastCameraPositionX = currentCameraPositionX;

            foreach (var layer in backgroundLayers)
            {
                layer.Move(distanceToMove);
            }
        }
    }
}
