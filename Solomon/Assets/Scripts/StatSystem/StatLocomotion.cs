using System;
using UnityEngine;

namespace Solomon
{
    [Serializable]
    public class StatLocomotion
    {
        /// <summary>
        /// 水平移动的最大速度, 单位: 米/秒. 同时决定滞空时长 -- 见 <see cref="JumpTime"/>.
        /// </summary>
        public Stat MoveSpeed = new Stat(6f);

        /// <summary>
        /// 跳跃最高点相对起跳点的高度, 单位: 米. 只影响高度, 不影响滞空时间.
        /// </summary>
        public Stat JumpHeight = new Stat(3f);

        /// <summary>
        /// 一次跳跃覆盖的水平距离, 单位: 米. 和 MoveSpeed 一起反推出滞空时间.
        /// 注意: 算出的时间会被 JumpTimeMin/Max 钳制, 被钳住时这个值就不起作用了.
        /// </summary>
        public Stat JumpSpan = new Stat(8f);

        /// <summary>
        /// 上升时间的下限, 单位: 秒. 防止速度很快时跳跃快到看不清.
        /// </summary>
        public float JumpTimeMin = 0.15f;

        /// <summary>
        /// 上升时间的上限, 单位: 秒. 防止速度很慢时滞空过长, 变成 "坐电梯".
        /// </summary>
        public float JumpTimeMax = 0.35f;

   
        private const float AccelTimeSlow = 0.35f;    // 起步到全速要多久
        private const float AccelTimeFast = 0.085f;
        private const float DecelTimeSlow = 0.42f;    // 松手到停下要多久
        private const float DecelTimeFast = 0.10f;
        private const float TurnBoostSlow = 1.0f;     // 反向时加速度乘几倍
        private const float TurnBoostFast = 3.0f;
        private const float AirControlSlow = 0.3f;    // 空中的加速度打几折
        private const float AirControlFast = 0.95f;

        /// <summary>
        /// 唯一的手感旋钮, 0 = 迟钝, 1 = 跟手. 上面八个常量之间按它插值.
        /// 调大之后: 起步更快, 急停更利落, 反向甩得更干脆, 空中更能改变方向.
        /// </summary>
        [Range(0f, 1f)] public float responsiveness = 0.7f;


        /// <summary>
        /// 到最高点用时, 单位: 秒. 由 JumpSpan 和 MoveSpeed 反推:
        /// 水平距离 = MoveSpeed * 2t, 所以 t = JumpSpan / (2 * MoveSpeed).
        /// 速度越快滞空越短, 而跳跃弧线的形状不变.
        /// </summary>
        public float JumpTime()
        {
            return Mathf.Clamp(JumpSpan.GetValue() / (2f * Mathf.Max(MoveSpeed.GetValue(), 0.01f)), JumpTimeMin, JumpTimeMax);
        }


        /// <summary>
        /// 在 slow 和 fast 之间按 responsiveness 做指数插值.
        /// 不能用线性插值: 人对时长的感知按比例走, 0.3 降到 0.15 很明显(减半),
        /// 0.06 降到 0.045 几乎无感(只 1.33 倍). 线性会把可感知的变化全挤在滑条一端,
        /// 指数插值让每段滑程都把时间乘以相同的倍数, 整条滑条的手感变化才均匀.
        /// </summary>
        private float LerpTime(float slow, float fast)
        {
            return slow * Mathf.Pow(fast / slow, responsiveness);
        }


        /// <summary>
        /// 起步/加速时的加速度, 单位: 米/秒². 加速度 = 速度 / 到达该速度所需时间.
        /// </summary>
        public float Acceleration()
        {
            return MoveSpeed.GetValue() / LerpTime(AccelTimeSlow, AccelTimeFast);
        }


        /// <summary>
        /// 松开方向键后的减速度, 单位: 米/秒²
        /// </summary>
        public float Deceleration()
        {
            return MoveSpeed.GetValue() / LerpTime(DecelTimeSlow, DecelTimeFast);
        }


        /// <summary>
        /// 反向时加速度要乘的倍数. 起步保留一点惯性有重量感, 但转向要立刻甩过去,
        /// 所以这两件事用不同的加速度. 只有一个加速度的话, 要么起步太滑要么转向太黏.
        /// </summary>
        public float TurnBoost()
        {
            return TurnBoostSlow * Mathf.Pow(TurnBoostFast / TurnBoostSlow, responsiveness);
        }


        /// <summary>
        /// 空中时加速度要乘的系数, 0~1. 低响应时跳出去就得认这条弧线,
        /// 高响应时空中可以随意改向. 这一维不受 "已经是瞬间" 的封顶影响,
        /// 所以两端的差别在整条滑条上都能感觉到.
        /// </summary>
        public float AirControl()
        {
            return AirControlSlow * Mathf.Pow(AirControlFast / AirControlSlow, responsiveness);
        }
    }
}
