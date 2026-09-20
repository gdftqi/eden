using TMPro;
using UnityEngine;

namespace Solomon
{
    public class UI_DamageText : MonoBehaviour
    {
        private const float Lifetime = 0.7f;      // 存活时长, 秒
        private const float RiseSpeed = 2.5f;     // 上升速度, 世界单位/秒
        private const float DriftRange = 0.4f;    // 横向随机偏移, 防止连击时数字重叠
        private const float CritScale = 1.5f;     // 暴击字号倍率

        private TextMeshPro text;
        private Vector3 velocity;
        private Color color;
        private float timer;

        private void Awake()
        {
            text = GetComponent<TextMeshPro>();
        }


        /// <summary>
        /// 生成后立刻调一次, 把伤害数据翻译成显示效果. 之后这个物体就自生自灭了.
        /// </summary>
        public void Setup(DamageInfo info)
        {
            text.text = Mathf.RoundToInt(info.Damage).ToString();

            color = ElementColor(info.Type);
            text.color = color;

            if (info.IsCrit)
            {
                transform.localScale *= CritScale;
            }

            velocity = new Vector3(Random.Range(-DriftRange, DriftRange), RiseSpeed, 0f);
        }


        private void Update()
        {
            timer += Time.deltaTime;

            transform.position += velocity * Time.deltaTime;

            float t = timer / Lifetime;
            color.a = t < 0.3f ? 1f : 1f - (t - 0.3f) / 0.7f;
            text.color = color;

            if (timer >= Lifetime)
            {
                Destroy(gameObject);
            }
        }


        private static Color ElementColor(ElementType type)
        {
            switch (type)
            {
                case ElementType.Fire: 
                    return new Color(1f, 0.45f, 0.1f);

                case ElementType.Ice: 
                    return new Color(0.4f, 0.85f, 1f);

                case ElementType.Lightning: 
                    return new Color(1f, 0.9f, 0.2f);

                case ElementType.Toxic: 
                    return new Color(0.5f, 0.9f, 0.3f);

                default: return Color.white;
            }
        }
    }
}
