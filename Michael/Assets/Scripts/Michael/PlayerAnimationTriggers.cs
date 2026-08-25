using UnityEngine;
using UnityEngine.Assertions;

namespace Michael
{
    public class PlayerAnimationTriggers : MonoBehaviour
    {
        private Player player;

        private void Awake()
        {
            player = GetComponentInParent<Player>();
            Assert.IsNotNull(player);
        }

        public void CurrentStateTrigger()
        {
            player.CallAnimationTrigger();
        }
    }
}
