using NUnit.Framework.Internal.Commands;
using Unity.Cinemachine;
using UnityEngine;
using UnityEngine.Assertions;

namespace Solomon
{
    public class ChapterEnvironment : MonoBehaviour
    {
        [Header("---------------- 雾 ----------------")]
        [SerializeField, Tooltip("雾色, 要贴近背景图的天空色, 差太远远景会撞色")]
        private Color fogColor = new Color(0.81f, 0.89f, 0.94f);

        [SerializeField, Tooltip("雾的起始距离, 比这近的完全不受雾影响")]
        private float fogStart = 30f;

        [SerializeField, Tooltip("雾的结束距离, 比这远的完全变成雾色")]
        private float fogEnd = 150f;

        [Header("---------------- 平行光 ----------------")]
        [SerializeField] private Light sun;
        [SerializeField, Tooltip("光照角度, x 是俯角")]
        private Vector3 sunEuler = new Vector3(50f, -30f, 0f);
        [SerializeField] private Color sunColor = Color.white;
        [SerializeField] private float sunIntensity = 2f;

        [Header("---------------- 镜头 ----------------")]
        [SerializeField] private CinemachineCamera cmPlayer;
        [SerializeField, Tooltip("越小越扁平越接近正交, 2.5D 用 25 左右")]
        private float fieldOfView = 25f;
        [SerializeField] private float cameraDistance = 14f;

        private void Awake()
        {
            Apply();
        }


        private void OnValidate()
        {
            Apply();
        }


        protected virtual void Apply()
        {
            if (sun == null)
            {
                sun = GetComponentInChildren<Light>();
            }
            Assert.IsNotNull(sun);

            if (cmPlayer == null)
            {
                cmPlayer = GetComponentInChildren<CinemachineCamera>();
            }
            Assert.IsNotNull(cmPlayer);

            var player = FindFirstObjectByType<Player>().transform;
            Assert.IsNotNull(player);

            RenderSettings.fog = true;
            RenderSettings.fogMode = FogMode.Linear;
            RenderSettings.fogColor = fogColor;
            RenderSettings.fogStartDistance = fogStart;
            RenderSettings.fogEndDistance = fogEnd;

            sun.transform.rotation = Quaternion.Euler(sunEuler);
            sun.color = sunColor;
            sun.intensity = sunIntensity;

            cmPlayer.Lens.FieldOfView = fieldOfView;
            cmPlayer.Follow = player;
            
            var composer = cmPlayer.GetComponent<CinemachinePositionComposer>();
            if (composer != null)
            {
                composer.CameraDistance = cameraDistance;
            }
        }
    }
}
