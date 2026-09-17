using System;
using UnityEngine;

namespace Solomon
{
    [Serializable]
    public class Stat
    {
        [SerializeField] private float baseValue;

        public float GetValue()
        {
            return baseValue;
        }

        public Stat(float baseValue = 0f)
        {
            this.baseValue = baseValue;
        }

        public Stat() { }
    }
}
