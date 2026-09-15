using Unity.Cinemachine;
using UnityEngine;
using UnityEngine.Assertions;

namespace Solomon
{
    [RequireComponent(typeof(CinemachineCamera), typeof(CinemachinePositionComposer))]
    public class PlayerCamera : MonoBehaviour
    {
        [Header("--------------------- 镜头 ---------------------")]
        [SerializeField] private float orthographicSize = 10f;   // 屏幕高度的一半, 单位: 米
        [SerializeField] private float cameraDistance = 10f;     // 相机沿 Z 退后的距离

        [Header("--------------------- 构图 ---------------------")]
        [SerializeField] private Vector2 screenPosition = new Vector2(0f, 0.1f);   // 角色在屏幕上的位置, 0 为正中
        [SerializeField] private Vector2 deadZoneSize = new Vector2(0.2f, 0.2f);   // 死区, 单位是屏幕比例不是米
        [SerializeField] private Vector3 damping = new Vector3(0.5f, 1.5f, 1f);    // 跟随阻尼, Y 要比 X 迟钝

        [Header("--------------------- 边界 ---------------------")]
        [SerializeField] private float confinerDamping = 0.5f;
        [SerializeField] private float confinerSlowingDistance = 0f;   // 离边界多近开始减速

        //[Header("--------------------- 下望 ---------------------")]
        //[SerializeField] private float lookDownDistance = 12f;    // 瞄准点下移多少米
        //[SerializeField] private float lookDownDelay = 0.4f;     // 按住多久才开始下移
        //[SerializeField] private float lookDownSpeed = 20f;      // 每秒移动多少米

        //[Header("--------------------- 下落跟随 ---------------------")]
        //[SerializeField] private float fallLookSpeed = 5f;       // 下落速度超过这个值才开始额外下移
        //[SerializeField] private float fallLookDistance = 10f;    // 下落时额外下移多少米

        private CinemachineCamera cam;
        private CinemachinePositionComposer composer;
        private CinemachineConfiner2D confiner;
        private PlayerController player;


        private void Awake()
        {
            Apply();
        }


        private void Apply()
        {
            if (cam == null)
            {
                cam = GetComponent<CinemachineCamera>();
            }

            if (composer == null)
            {
                composer = GetComponent<CinemachinePositionComposer>();
            }

            if (confiner == null)
            {
                confiner = GetComponent<CinemachineConfiner2D>();
            }

            if (player == null)
            {
                player = FindFirstObjectByType<PlayerController>();
            }

            Assert.IsNotNull(player, "场景里没有 PlayerController, 相机没有跟随目标");

            cam.Target.TrackingTarget = player.transform;
            cam.Lens.OrthographicSize = orthographicSize;

            composer.CameraDistance = cameraDistance;
            composer.Composition.ScreenPosition = screenPosition;
            composer.Composition.DeadZone.Enabled = true;
            composer.Composition.DeadZone.Size = deadZoneSize;
            composer.Damping = damping;

            if (confiner != null)
            {
                confiner.Damping = confinerDamping;
                confiner.SlowingDistance = confinerSlowingDistance;
            }
        }
    }
}
