using System;
using UnityEngine;

namespace Solomon
{
    [Serializable]
    public class StatLocomotionGroup
    {
        public StatLocomotionGroup(float moveSpeed, float jumpForce)
        {
            this.moveSpeed = new Stat(moveSpeed);
            this.jumpForce = new Stat(jumpForce);
        }

        [Tooltip("移动速度, 世界单位/秒. 同时决定跳跃的上升下落快慢: 跑得越快, 同一条弧线走得越快")]
        public Stat moveSpeed;

        [Tooltip("跳跃力, 世界单位. 只影响跳多高, 不影响时间. 角色高 2, 参考值 3.4")]
        public Stat jumpForce;
    }
}
