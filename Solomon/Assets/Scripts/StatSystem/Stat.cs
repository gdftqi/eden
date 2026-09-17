using System;
using UnityEngine;

namespace Solomon
{
    [Serializable]
    public class Stat
    {
        [SerializeField] private float baseValue = 0f;

        public float GetValue()
        {
            return baseValue;
        }

        public Stat(float baseValue)
        {
            this.baseValue = baseValue;
        }

        public Stat() { }
    }
}
