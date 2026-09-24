using UnityEngine;

namespace Solomon
{
    public class Enemy : Character
    {
        private float pauseTimer;
        [SerializeField] private float pauseMin = 2f;
        [SerializeField] private float pauseMax = 3f;


        protected override void FixedUpdate()
        {
            base.FixedUpdate();

            if (pauseTimer > 0f)
            {
                pauseTimer -= Time.fixedDeltaTime;
                Move(0f);

                if (pauseTimer <= 0f)
                {
                    SetFacing(-faceDirection);
                }

                return;
            }

            if (groundDetected && (wallDetected || !frontGroundDetected))
            {
                pauseTimer = Random.Range(pauseMin, pauseMax);
                Move(0f);
                return;
            }

            Move(faceDirection);
        }


        protected override void Update()
        {
            base.Update();
            PlayAnim(pauseTimer > 0f ? "Idle" : "Move", true);
        }
    }
}
