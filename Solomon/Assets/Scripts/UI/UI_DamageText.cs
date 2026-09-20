using TMPro;
using UnityEngine;
using UnityEngine.Pool;

namespace Solomon
{
    public class UI_DamageText : MonoBehaviour
    {
        private const float Lifetime = 0.7f;      // 存活时长, 秒
        private const float RiseSpeed = 2.5f;     // 上升速度, 世界单位/秒
        private const float DriftRange = 0.4f;    // 横向随机偏移, 防止连击时数字重叠
        private const float CritScale = 1.5f;     // 暴击字号倍率

        private const string PrefabPath = "Prefabs/UI_DamageText";
        private const int PoolCapacity = 16;      // 预期同屏数量, 池子按这个预留
        private const int PoolMaxSize = 128;      // 超出就直接销毁, 不再回收, 防止异常情况下无限涨

        private static ObjectPool<UI_DamageText> pool;
        private static bool poolTried;            // 加载失败时别每次命中都重试一遍 Resources.Load

        private TextMeshPro text;
        private Vector3 velocity;
        private Color color;
        private float timer;


        /// <summary>
        /// 唯一的外部入口
        /// </summary>
        public static void Spawn(Vector3 pos, DamageInfo info)
        {
            if (!poolTried)
            {
                poolTried = true;
                pool = CreatePool();
            }

            if (pool == null)
            {
                return;
            }

            UI_DamageText item = pool.Get();
            if (item == null)
            {
                return;
            }

            item.transform.position = pos;
            item.Setup(info);
        }


        private static ObjectPool<UI_DamageText> CreatePool()
        {
            GameObject prefab = Resources.Load<GameObject>(PrefabPath);

            if (prefab == null)
            {
                Debug.LogErrorFormat("{0} 不存在", PrefabPath);
                return null;
            }

            return new ObjectPool<UI_DamageText>(
                createFunc: () =>
                {
                    GameObject go = Instantiate(prefab);
                    DontDestroyOnLoad(go);
                    return go.GetComponent<UI_DamageText>();
                },
                actionOnGet: item =>
                {
                    if (item != null)
                    {
                        item.gameObject.SetActive(true);
                    }
                },
                actionOnRelease: item =>
                {
                    if (item != null)
                    {
                        item.gameObject.SetActive(false);
                    }
                },

                actionOnDestroy: item =>
                {
                    if (item != null)
                    {
                        Destroy(item.gameObject);
                    }
                },
                collectionCheck: true,
                defaultCapacity: PoolCapacity,
                maxSize: PoolMaxSize);
        }


        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.SubsystemRegistration)]
        private static void ResetStatics()
        {
            pool = null;
            poolTried = false;
        }


        private void Awake()
        {
            text = GetComponent<TextMeshPro>();
        }


        private void Setup(DamageInfo info)
        {
            text.text = Mathf.RoundToInt(info.Damage).ToString();

            color = ElementColor(info.Type);
            text.color = color;

            timer = 0f;
            transform.localScale = info.IsCrit ? Vector3.one * CritScale : Vector3.one;

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
                pool.Release(this);
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

                default:
                    return Color.white;
            }
        }
    }
}
