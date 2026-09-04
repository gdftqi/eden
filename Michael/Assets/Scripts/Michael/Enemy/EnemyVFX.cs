using UnityEngine;

namespace Michael
{
    public class EnemyVFX : EntityVFX
    {
        [Header("Counter Attack Window")]
        [SerializeField] private GameObject attackAlert;

        public void EnableAttackAlert(bool enable)
        {
            attackAlert.SetActive(enable);
        }
    }
}
