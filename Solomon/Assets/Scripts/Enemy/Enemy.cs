using UnityEngine;

namespace Solomon
{
    public class Enemy : Character
    {
        protected override void Awake()
        {
            base.Awake();
            //collider.isTrigger = true;
        }
    }
}
