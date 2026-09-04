using UnityEngine;

namespace Michael
{
    public interface ICounterable
    {
        public bool CanBeCountered { get; }

        public void HandleCounter();
    }
}
