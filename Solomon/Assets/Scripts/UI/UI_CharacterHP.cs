using UnityEngine;

namespace Solomon
{
    public class UI_CharacterHP : MonoBehaviour
    {
        private Transform fill;
        private Character owner;

        private void Awake()
        {
            owner = GetComponentInParent<Character>();
            fill = transform.Find("Fill");
        }

        private void Update()
        {
            if (owner == null || fill == null)
            {
                return;
            }

            float max = owner.stat.HP.Max.GetValue();
            float ratio = max > 0f ? Mathf.Clamp01(owner.stat.HP.Current() / max) : 0f;

            fill.localScale = new Vector3(ratio, 1f, 1f);
        }
    }
}
