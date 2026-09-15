using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Tilemaps;

namespace Solomon
{
    [RequireComponent(typeof(Tilemap))]
    public class Platform : MonoBehaviour
    {
        [SerializeField] private float tolerance = 0.05f;      // 脚底和顶面的容差, 防止贴合时抖动
        [SerializeField] private float dropThroughTime = 0.3f; // 主动下穿持续多久

        private readonly List<BoxCollider2D> blocks = new List<BoxCollider2D>();
        private readonly List<bool> ignoring = new List<bool>();

        private PlayerController player;
        private Collider2D playerCollider;
        private float dropThroughTimer;

        private void Awake()
        {
            var tilemapCollider = GetComponent<TilemapCollider2D>();

            if (tilemapCollider != null)
            {
                tilemapCollider.enabled = false;
            }

            Build();
        }


        private void Start()
        {
            player = FindFirstObjectByType<PlayerController>();

            if (player != null)
            {
                playerCollider = player.GetComponent<Collider2D>();
            }
        }


        private void FixedUpdate()
        {
            if (playerCollider != null)
            {
                if (dropThroughTimer > 0f)
                {
                    dropThroughTimer -= Time.fixedDeltaTime;

                    for (int i = 0; i < blocks.Count; i++)
                    {
                        SetIgnore(i, true);
                    }

                    return;
                }

                bool falling = player.VerticalSpeed <= 0f;
                float feet = playerCollider.bounds.min.y;

                for (int i = 0; i < blocks.Count; i++)
                {
                    bool collide = falling && feet >= blocks[i].bounds.max.y - tolerance;
                    SetIgnore(i, !collide);
                }
            }
        }


        public void DropThrough()
        {
            dropThroughTimer = dropThroughTime;
        }


        private void Build()
        {
            var map = GetComponent<Tilemap>();
            BoundsInt area = map.cellBounds;

            for (int y = area.yMin; y < area.yMax; y++)
            {
                int x = area.xMin;

                while (x < area.xMax)
                {
                    if (!map.HasTile(new Vector3Int(x, y, 0)))
                    {
                        x++;
                        continue;
                    }

                    int start = x;

                    while (x < area.xMax && map.HasTile(new Vector3Int(x, y, 0)))
                    {
                        x++;
                    }

                    CreateBlock(map, start, x - 1, y);
                }
            }
        }


        private void CreateBlock(Tilemap map, int xMin, int xMax, int y)
        {
            Vector3 min = map.CellToLocal(new Vector3Int(xMin, y, 0));
            Vector3 max = map.CellToLocal(new Vector3Int(xMax + 1, y + 1, 0));

            var go = new GameObject($"P_{y}_{xMin}");
            go.layer = gameObject.layer;
            go.transform.SetParent(transform, false);
            go.transform.localPosition = (min + max) * 0.5f;

            var box = go.AddComponent<BoxCollider2D>();
            box.size = new Vector2(max.x - min.x, max.y - min.y);

            blocks.Add(box);
            ignoring.Add(false);
        }


        private void SetIgnore(int index, bool value)
        {
            if (value == ignoring[index])
            {
                return;
            }

            ignoring[index] = value;
            Physics2D.IgnoreCollision(playerCollider, blocks[index], value);
        }
    }
}
