using System;
using UnityEngine;

namespace Solomon
{
    [Serializable]
    public class StatLocomotion
    {
        public Stat MoveSpeed = new Stat(6f);
        public Stat JumpHeight = new Stat(3f);    // 最高点高度, 单位: 米
        public Stat JumpSpan = new Stat(8f);     // 一次跳跃的水平距离, 单位: 米
        public Stat JumpTimeMin = new Stat(0.15f);   // 上升时间下限, 单位: 秒
        public Stat JumpTimeMax = new Stat(0.35f);   // 上升时间上限, 单位: 秒

        [SerializeField] private float AccelTimeSlow = 0.35f;
        [SerializeField] private float AccelTimeFast = 0.085f;
        [SerializeField] private float DecelTimeSlow = 0.42f;
        [SerializeField] private float DecelTimeFast = 0.10f;
        [SerializeField] private float TurnBoostSlow = 1.0f;
        [SerializeField] private float TurnBoostFast = 3.0f;
        [SerializeField] private float AirControlSlow = 0.3f;
        [SerializeField] private float AirControlFast = 0.95f;

        [Range(0f, 1f)] public float responsiveness = 0.7f;


        public float JumpTime()
        {
            return Mathf.Clamp(JumpSpan.GetValue() / (2f * Mathf.Max(MoveSpeed.GetValue(), 0.01f)), JumpTimeMin.GetValue(), JumpTimeMax.GetValue());
        }


        private float LerpTime(float slow, float fast)
        {
            return slow * Mathf.Pow(fast / slow, responsiveness);
        }


        public float Acceleration()
        {
            return MoveSpeed.GetValue() / LerpTime(AccelTimeSlow, AccelTimeFast);
        }


        public float Deceleration()
        {
            return MoveSpeed.GetValue() / LerpTime(DecelTimeSlow, DecelTimeFast);
        }


        public float TurnBoost()
        {
            return TurnBoostSlow * Mathf.Pow(TurnBoostFast / TurnBoostSlow, responsiveness);
        }


        public float AirControl()
        {
            return AirControlSlow * Mathf.Pow(AirControlFast / AirControlSlow, responsiveness);
        }
    }
}
