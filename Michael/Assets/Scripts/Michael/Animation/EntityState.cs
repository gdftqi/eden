using Michael;
using Michael.Animation;
using UnityEngine;
using UnityEngine.Windows;

public abstract class EntityState
{
    protected StateMachine stateMachine;
    protected string stateConditionName;
    protected Animator anim;
    protected Rigidbody2D rb;

    protected float stateTimer = 0f;
    protected bool triggerCalled = false;

    public EntityState(StateMachine sm, string conditionName)
    {
        stateMachine = sm;
        stateConditionName = conditionName;
    }

    /// <summary>
    /// 每次当即将进入到某个状态时, 调用该方法
    /// </summary>
    public virtual void Enter()
    {
        anim.SetBool(stateConditionName, true);
        triggerCalled = false;
    }

    /// <summary>
    /// 每次当即将进入下一个状态时, 调用该方法
    /// </summary>
    public virtual void Exit()
    {
        anim.SetBool(stateConditionName, false);
    }

    /// <summary>
    /// 逻辑更新
    /// </summary>
    public virtual void Update()
    {
        stateTimer -= Time.deltaTime;
    }

    public void CallAnimationTrigger()
    {
        triggerCalled = true;
    }
}
