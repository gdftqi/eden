using System;
using Unity.VisualScripting;
using UnityEngine;

namespace Michael
{
    public class PlayerCombat : EntityCombat
    {
        [Header("Counter attack details")]
        [SerializeField] private float counterDuration = 1f;

        public bool CounterAttackPerformed()
        {
            bool hasCounterSomebody = false;
            var targets = GetDetectedColliders();
            foreach (var target in targets)
            {
                ICounterable counterable = target.GetComponent<ICounterable>();
                if (counterable != null)
                {
                    counterable.HandleCoutner();
                    hasCounterSomebody = true;
                }
            }

            return hasCounterSomebody;
        }

        public float GetCounterDuration()
        {
            return counterDuration;
        }
    }
}
