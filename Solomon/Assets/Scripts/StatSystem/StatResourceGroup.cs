using System;
using UnityEngine;

namespace Solomon
{
    [Serializable]
    public class StatResourceGroup
    {
        [Tooltip("生命值")]
        public Stat MaxHP;

        [Tooltip("魔法值")]
        public Stat MaxMP;
    }
}
