using System;
using UnityEngine;

namespace Solomon
{
    [Serializable]
    public class StatLocomotionGroup
    {
        public StatLocomotionGroup(float moveSpeed)
        {
            this.moveSpeed = new Stat(moveSpeed);
        }

        [Tooltip("移动速度")]
        public Stat moveSpeed;
    }
}
