using Michael;
using Michael.Animation;
using UnityEngine;

public class EnemyGroundedState : EnemyState
{
    public EnemyGroundedState(Enemy enemy, StateMachine sm, string conditionName) : base(enemy, sm, conditionName)
    {
    }

    public override void Update()
    {
        base.Update();

        if (enemy.PlayerDetection())
        {
            stateMachine.ChangeState(enemy.BattleState);
            return;
        }
    }
}
