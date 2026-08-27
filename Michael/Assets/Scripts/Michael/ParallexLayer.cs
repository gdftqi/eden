using System;
using UnityEngine;

namespace Michael
{
    [Serializable]
    public class ParallexLayer
    {
        [SerializeField] private Transform background;
        [SerializeField] private float parallexMultiplier;

        public void Move(float distanceToMove)
        {
            background.position += Vector3.right * distanceToMove * parallexMultiplier;
        }
    }
}
