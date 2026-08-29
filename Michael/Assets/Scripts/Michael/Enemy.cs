using Michael.Animation;
using UnityEngine;

namespace Michael
{
    public class Enemy : Entity
    {
        public EntityState IdleState { get; set; }
        public EntityState MoveState { get; set; }
        public EntityState AttackState { get; set; }

        [Header("Locomotion properties")]
        public float idleTime = 2;
        public float moveSpeed = 1.5f;
        [Range(0f, 2f)]
        public float moveAnimSpeedMultiplier = 1f;
    }
}
