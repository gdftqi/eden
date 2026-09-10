using System;
using UnityEngine;

namespace Solomon
{
    [Serializable]
    public class Stat
    {
        [SerializeField, Tooltip("基础值")]
        private float baseValue;

        public Stat(float v = 0f)
        {
            baseValue = v;
        }

        public float GetValue()
        {
            return baseValue;
        }
    }
}
