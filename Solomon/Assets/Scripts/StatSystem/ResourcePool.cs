using System;
using UnityEngine;

namespace Solomon
{
    [Serializable]
    public class ResourcePool
    {
        [SerializeField] private float current;

        public float Current()
        {
            return current;
        }


        public void Fill(float current = -1f)
        {
            this.current = current < 0f ? Max.GetValue() : Mathf.Min(current, Max.GetValue());
        }


        /// <summary>
        /// 扣减, 钳到 0 为止. 传负数无效, 想加请用专门的恢复方法.
        /// </summary>
        public void Reduce(float amount)
        {
            if (amount <= 0f)
            {
                return;
            }

            current = Mathf.Max(0f, current - amount);
        }

        public Stat Max = new Stat(1f);
        public Stat RegenPerSecond;
    }
}
