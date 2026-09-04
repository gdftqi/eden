using UnityEngine;

namespace Michael
{
    public class PlayerCombat : EntityCombat
    {
        [Header("Counter attack details")]
        [SerializeField] private float counterRecovery = 0.1f;

        public bool CounterAttackPerformed()
        {
            bool hasPerformedCounter = false;
            var targets = GetDetectedColliders();
            foreach (var target in targets)
            {
                ICounterable counterable = target.GetComponent<ICounterable>();
                if (counterable == null)
                {
                    continue;
                }

                if (counterable.CanBeCountered)
                {
                    counterable.HandleCounter();
                    hasPerformedCounter = true;
                }
            }

            return hasPerformedCounter;
        }

        public float GetCounterRecoveryDuration()
        {
            return counterRecovery;
        }
    }
}
