using System;
using UnityEditor;
using UnityEngine;

namespace Michael
{
    [Serializable]
    public class ParallexLayer
    {
        [SerializeField] private Transform background;
        [SerializeField] private float parallexMultiplier = 0f;
        [SerializeField] private float imageWidthOffset = 10f;

        private float imageFullWidth = 0f;
        private float imageHalfWidth = 0f;

        public void CaculateImageWidth()
        {
            imageFullWidth = background.GetComponent<SpriteRenderer>().bounds.size.x;
            imageHalfWidth = imageFullWidth / 2;
        }

        public void Move(float distanceToMove)
        {
            background.position += Vector3.right * distanceToMove * parallexMultiplier;
        }

        public void LoopBackground(float left, float right)
        {
            float imageRightEdge = background.position.x + imageHalfWidth - imageWidthOffset;
            float imageLeftEdge = background.position.x - imageHalfWidth + imageWidthOffset;

            if (imageRightEdge < left)
            {
                background.position += Vector3.right * imageFullWidth;
            }
            else if (imageLeftEdge > right)
            {
                background.position += Vector3.right * -imageFullWidth;
            }
        }
    }
}
