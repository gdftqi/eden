using System;
using UnityEngine;

namespace Michael
{
    [Serializable]
    public class Stat
    {
        [SerializeField] private float baseValue;

        public float GetValue()
        {
            return baseValue;
        }
    }
}
