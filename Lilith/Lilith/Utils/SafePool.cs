using System;
using System.Collections.Generic;


namespace Lilith.Utils
{
    public class SafePool<T>
    {
        readonly Stack<T> objects = new Stack<T>();
        readonly Func<T> objectGenerator;
        readonly Action<T> objectResetter;
        readonly object locker = new object();

        public SafePool(Func<T> objectGenerator, Action<T> objectResetter)
        {
            this.objectGenerator = objectGenerator;
            this.objectResetter = objectResetter;
        }

        public T Take()
        {
            lock (locker)
            {
                if (objects.Count > 0)
                {
                    return objects.Pop();
                }
            }

            return objectGenerator();
        }

        public void Return(T item)
        {
            objectResetter(item);
            lock (locker)
            {
                objects.Push(item);
            }
        }

        public void Clear()
        {
            lock (locker)
            {
                objects.Clear();
            }
        }
    }
}
