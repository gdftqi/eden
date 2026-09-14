# Solomon — Unity 2D 骨骼角色装配手册

> 适用: Unity 6000.3 + URP 2D + `com.unity.2d.animation` 13.x
> 场景: 拿到外包/商店的 `.psb` + `.anim`, 没有现成 prefab, 要自己装配成可用角色

---

## 0. 为什么会有这份文档

Unity 2D Animation 的角色数据是**分散**的:

| 数据 | 存在哪 | 拷文件带得走吗 |
|---|---|---|
| 骨骼层级、蒙皮网格、权重 | `.psb.meta` | 带得走 |
| 动画曲线 | `.anim` | 带得走 |
| **IK (effector / solver / Target)** | **prefab / 场景** | **带不走** |
| **Animator、物理链** | **prefab / 场景** | **带不走** |

所以只拿到 `.psb` + `.anim` 时, IK 和 Animator 必须自己重建。这份文档就是重建的方法。

对比: Spine / DragonBones 把骨架+绑定+IK+动画全打包进一个数据文件, 不存在这个问题。这是 Unity 2D Animation 的结构性弱点。

---

## 1. 三个值的性质完全不同

装配时要填三类数值, 它们的性质不一样, 搞混了就会到处找"标准答案":

| 值 | 性质 | 怎么得到 |
|---|---|---|
| `root` 骨骼的位置 | **设计决定** | 自己定, 没有对错 |
| effector 的位置 | **手放, 目测** | Scene 视图拖 |
| Target 的位置/旋转 | **派生值** | 按钮自动生成, 永远别手填 |

### 证据: effector 不在关节上

本项目 Sara 的实测数据:

```
thigh_L_1        localPos.x = 0.1160
leg_L_1          localPos.x = 0.3256    大腿长度
foot_L_01        localPos.x = 0.3520    小腿长度 (脚踝在这)
leg_L_effector   localPos.x = 0.4700    比脚踝还远 0.118

thigh_R_1        localPos.x = 0.1513
leg_R_1          localPos.x = 0.2958
foot_R_01        localPos.x = 0.3464    脚踝
leg_R_effector   localPos.x = 0.4150    比脚踝还远 0.069
```

左右腿多出来的量还不一样 (0.118 vs 0.069) —— 说明是作者手放的, 不是算出来的。
**别去找公式, 它没有公式。**

---

## 2. 正确的建立顺序

顺序对了, 一个数都不用调。顺序错了, 后面要手工补一堆。

### 第 1 步: 先定 `root` (最关键, 必须第一个做)

1. 选中角色根物体, Position 归零 `(0, 0, 0)`
2. 选中 `root` 骨骼
3. 在 Scene 视图里拖, 直到:
   - 脚底落在 `y = 0`
   - 身体中线落在 `x = 0`

**为什么是脚底**: 游戏里 `transform.position` 就是这个点。落地判断、出生点、相机跟随、生成特效全以它为准。枢轴在脚底时 `transform.position.y == 地面高度` 就是站在地上, 代码最省事。

**看不准**: 临时加一个 `BoxCollider2D` 当尺子, 把下边缘对准 `y=0` 的网格线, 调完删掉。

> 这一步必须在建 IK 之前完成。先建 IK 再改 `root`, 会导致所有 Target 的静止位置全部失效, 要手工逐个补。

### 第 2 步: 建 effector

每条 IK 链, 在**第二根骨头**下建一个空物体:

1. 右键那根骨头 -> `Create Empty`
2. Position 先填 `(0, 0, 0)` (落在骨头根部)
3. 用移动工具**沿骨头方向往外拖**, 拖到希望被"钉住"的那个点
4. 检查 Inspector, 确保 **Y = 0**, 只有 X 有值 (骨骼本地 X 轴 = 骨头朝向)

| IK 链 | effector 放在 |
|---|---|
| 腿 (大腿 + 小腿) | 脚踝 ~ 脚跟之间 |
| 脚 (脚掌 + 脚趾) | 脚尖 |
| 手臂 (大臂 + 小臂) | 手腕 |

这个位置决定 IK 链的总长度 `|骨1| + |骨2->effector|`, 也就是这条肢体能伸多远。差百分之十几完全不影响。

### 第 3 步: 建 solver 和 Target

1. 角色根物体 `Add Component` -> `IK Manager 2D`
2. 点 `+` -> `Limb` -> 生成的子物体改名 (例: `leg_L_solver`)
3. `Effector` 槽拖入对应的 effector
4. **点面板底部的 `Create Target`**

按钮做的事 (源码 `Solver2DEditor.DoCreateTargetButton`):

```
新建 GameObject, 命名 = solver 名 + "_Target"
SetParent(solver)
target.position = effector.position     <-- 位置对齐
target.rotation = effector.rotation     <-- 旋转对齐
```

**永远用按钮, 永远不要手填 Target 的数值。** 按钮保证 "Target 静止值 == effector 静止值", 这个等式一破, 编辑模式下角色就变形。

> 命名注意: 按钮用的是**当前** solver 的名字, 所以要**先改名 solver, 再点按钮**。
> 动画曲线的路径是写死的 (例 `leg_L_solver/leg_L_solver_Target`), 大小写敏感, 差一个字符就绑不上。

### 第 4 步: 定 Flip

在 Scene 视图拖一下 Target, 看膝盖/脚踝往哪弯, 弯反了就勾 `Flip`。

两根骨头够到同一个目标点只有两个解, `Flip` 就是在两个解之间切。不用算, 试一下就知道。

### 第 5 步: 挂 Animator, 验证

打开 Animation 窗口 (`Ctrl+6`), 选中 clip:
- 左边一整列 = 这个 clip 驱动的所有属性
- 红色 `Missing!` = clip 要找的物体还没建, 或者名字不对
- **红字全消 = 结构对齐了**

---

## 3. 症状 -> 原因 -> 调法

| 现象 | 原因 | 调哪个 |
|---|---|---|
| **运行时正常, 编辑模式变形** | Target 静止值 != effector 静止值 | 重点 `Create Target` |
| 编辑模式人缩成一团、膝盖过弯 | Target 比 effector 高/近 | 同上 |
| 编辑模式腿被拉直、人变高 | Target 比 effector 低/远 | 同上 |
| 膝盖/脚踝朝反方向弯 | `Flip` 反了 | 勾/取消 `Flip` |
| 脚踝角度死住不跟动画 | `Constrain Rotation` 开着, 但 clip 没有 Target 的旋转曲线 | 改 Target 的旋转值, 或取消 `Constrain Rotation` |
| 肢体伸不直, 永远微弯 | effector 放太远, 链太长 | effector 往回拖 |
| Animation 窗口红色 `Missing!` | 物体没建, 或名字/层级不对 | 按 clip 里的路径建 |
| **角色整体浮空 / 陷进地里** | `root` 的 Y | 调 `root` |
| **运行时就不对** | `root` 位置, 或 psb 绑定本身 | 先查 `root`, 再查 psb |

### 三个高频陷阱

**陷阱 1: IK 会往 effector 上写数据**

`Constrain Rotation` 打勾时, IK 每帧把 Target 的旋转写到 effector 上。所以:
- 手改 effector 的值, 松手就被改回去
- 修 effector 之前**必须先取消勾选 `IK Manager 2D` 组件**, 改完再勾回来

**陷阱 2: 基准姿势是烤进文件的**

`LimbSolver2D` 里有个序列化字段 `m_DefaultLocalRotations`, 在 `Initialize()` 时抓一次当前骨骼姿势存下来。`Solve from Default Pose` 打勾时, 每帧解算前先恢复到这份基准。

- `Initialize()` **只在链失效时才重跑** (源码: `if (!solver.isValid) solver.Initialize();`)
- 所以基准一旦抓错, 改别的参数都没用, 必须让链失效一次

**强制重抓的办法**: `Effector` 槽先换成别的物体, 再换回来。

**抓的时机也要对**: 抓的时候如果 Animation 窗口正在预览动画, 抓到的就是动画姿势而不是静止姿势。**先关掉 Animation 预览再重抓。**

**陷阱 3: 动画不驱动的东西才归你管**

判断方法: Animation 窗口左边那列就是 clip 驱动的全部属性。不在列表里的, 动画永远不会碰, 你填什么就是什么。

本项目 Sara 的例子:
- `sara_idle.anim` 的曲线从 `root/pelvis_1` 开始, **没有 `root`** -> `root` 归你管
- 4 个 Target 只有 `Position` 曲线, **没有 `Rotation`** -> Target 的旋转归你管

---

## 4. 最重要的一条原则

> **动画驱动什么, 什么就不用你操心; 动画不驱动的, 才是你要负责的。**

以及:

> **运行时正常 = 验收通过。编辑模式的站姿只影响摆场景时看着舒不舒服, 不影响游戏。**

---

## 5. 验收自检清单

拿到外包/商店的 `.psb` + `.anim`, 按这个跑一遍, 十分钟:

```
[ ] 1. 拖 psb 进场景, Skinning Editor 里刷一下看蒙皮变形正不正常
[ ] 2. 定 root: 脚底贴 y=0, 身体中线贴 x=0
[ ] 3. 建 effector: 每条链一个, 沿骨头方向拖到关节点, Y 归零
[ ] 4. 建 solver + 点 Create Target (先改名 solver 再点按钮)
[ ] 5. 拖 Target 试 Flip 方向
[ ] 6. 挂 Animator + clip, 打开 Animation 窗口确认没有红色 Missing!
[ ] 7. 点 Play 看动画 -> 正常 = 验收通过 (唯一硬标准)
[ ] 8. 编辑模式站姿怪 -> 回第 4 步重点一次 Create Target
```

---

## 6. 外包交付要求

美术外包的默认交付物是**美术资产**, 不是引擎里能跑的东西。不写进需求就不会给, 而且这不算失职。

发需求时把这些列进去:

```
1. .psb 源文件 + Unity .meta (含完整骨骼绑定与权重)
2. 在 Unity <版本号> 中可直接使用的角色 Prefab
   - 含 IK 配置 (IKManager2D + LimbSolver2D)
   - 含 SpriteSkin 全部正确绑定
3. 全部动画 .anim 文件
4. 一个可运行的演示场景
5. 骨骼命名规范表
```

**第 5 条最容易被忽略但最重要**: 要复用动画 (一套 clip 给多个角色用), 骨骼名和层级必须完全一致。不提前定死, 第二个角色的动画就绑不上。

**推荐做法**: 把本项目已经跑通的 Sara 当**模板工程**发给外包, 让他们照着骨骼命名和 IK 结构做, 你按第 5 节的清单验收。

**注意 Unity 版本**: 如果要求对方交付 prefab, 合同里写死 Unity 版本和 `com.unity.2d.animation` 的包版本, 否则跨版本打开可能出问题。

---

## 附: 本项目 Sara 的实际数值 (可作模板参考)

### 层级结构

```
Player                              IKManager2D, Animator, SpriteLibrary
 |- root                            (-0.027, -0.064)  Z=90.466   <- 动画不驱动
 |   |- pelvis_1
 |       |- abdomen - chest - neck_1 - head_1 - hairSide_01/02, hairTop, tail_01
 |       |             |- shoulder_L - arm_L_1 - forearm_L_1 - hand_L
 |       |             |- shoulder_R - arm_R_1 - forearm_R_1 - hand_R
 |       |- hip
 |           |- thigh_L_1 - leg_L_1 - leg_L_effector
 |           |                      |- foot_L_01 - foot_L_02 - foot_L_effector
 |           |- thigh_R_1 - leg_R_1 - leg_R_effector
 |                                  |- foot_R_01 - foot_R_02 - foot_R_effector
 |- leg_L_solver   - leg_L_solver_Target
 |- leg_R_solver   - leg_R_solver_Target
 |- foot_L_solver  - foot_L_solver_Target
 |- foot_R_solver  - foot_R_solver_Target
 |- (24 个精灵部件: head, neck, arm_L, thigh_L, upperBoot_L, tail ...)
```

注: 骨骼名里的 `_1` 后缀是 Unity 自动去重加的 (psb 里骨头和图层同名)。

### 关键数值

```
root              pos (-0.027, -0.064)   rot Z 90.466

leg_L_effector    pos ( 0.470,  0)       leg_R_effector   pos ( 0.415, 0)
foot_L_effector   pos ( 0.257,  0)       foot_R_effector  pos ( 0.244, 0)

leg_L_solver_Target    pos (-0.0307, -0.0657)   rot Z  -94.52   Flip 否
leg_R_solver_Target    pos (-0.1167, -0.0139)   rot Z  -99.11   Flip 否
foot_L_solver_Target   pos ( 0.3287, -0.0223)   rot Z   -2.62   Flip 是
foot_R_solver_Target   pos ( 0.2353, -0.0333)   rot Z   -2.48   Flip 是

四个 solver 统一: ConstrainRotation 开, SolveFromDefaultPose 开, Weight 1
IKManager2D:      AlwaysUpdate 开, Weight 1
```

### 尚未复刻的部分

官方 `Sara.prefab` 还有一套**马尾物理链**本项目没做:

```
psb 自带的 tail 部件被禁用 (m_IsActive = 0)
另建 tail 物体: Sara_ponytail.png + SpriteSkin + Rigidbody2D
底下串 19 节: bone_1 .. bone_18, bone_tip
  每节 Rigidbody2D + HingeJoint2D (第一节多一个 FixedJoint2D)
PonyTailFollow.cs 让它跟随头部
```

不做的表现: 辫子是刚性的, 只跟着 `tail_01` 骨头转, 不会飘。视觉差异明显但不影响功能。

### 调试工具

`Assets/Editor/BoneDumper.cs` — 只读诊断工具, 菜单 `Tools/Dump Bones`。
选中一个或多个角色根物体, 点一下, 把整个层级的 localPosition / localEulerZ / localScale /
worldPosition / worldEulerZ 导出成 `BoneDump_<名字>.txt` 放在工程根目录。

用途: 两个角色并排 dump 后逐行 diff, 直接定位是哪根骨头、差在位置还是旋转。
没有参照物时也能用 —— 看 `root` 的 Y、看 Target 和 effector 的位置差。
