#pragma once
// ============================================================================
// 002_Order_Independent_Transparency - Scene
//
// 业务侧轻量场景：Cornell Box（不透明）+ 若干 Stanford Dragon 实例（半透明）。
// 龙共用一份 GpuModelHandle，并排同朝向、每只用不同颜色；不做文件 IO、不做 GPU 上传。
// ============================================================================
#include <vector>

#include "AssetLoader/AssetTypes.h"
#include "RendererInterface/TitusGfx.h"

// 半透明龙实例：共用同一份 GpuModel，只换矩阵和颜色。
// worldCenter 是龙局部 AABB 中心过 modelMatrix 后的世界坐标，供按视距排序使用，
// 由 Scene::ApplyDragonLayout 在生成矩阵时一并算出。
struct DragonInstance
{
    TitusMath::Mat4 modelMatrix{1.0f};
    TitusMath::Vec3 albedo{0.25f, 0.70f, 0.90f};
    TitusMath::Vec3 worldCenter{0.0f};
};

constexpr int kDragonInstanceCount = 3;

// 半透明龙的摆放方式。Pinwheel 是本示例真正要演示的构型，Row 保留下来当反面
// 对照：一字排开时各实例在屏幕上几乎不重叠，排序错误无从显现。
enum class DragonLayout
{
    Pinwheel = 0, // 绕 Y 轴均分一圈、首尾插进彼此身体，构成循环遮挡
    Row = 1,      // 沿盒子 X 轴一字排开，实例间基本不重叠
};

struct DragonLayoutParams
{
    DragonLayout layout = DragonLayout::Pinwheel;
    // 龙的高度占盒子内高的比例。Cornell 内腔只有约 2.0 宽，风车要在水平面上铺开
    // 三只龙，龙必须比一字排开时小不少，否则只能糊成一团。
    float heightFill = 0.35f;
    // 仅 Pinwheel：咬合深度。0 = 各自外推到贴着盒壁（重叠最少），
    // 1 = 全部堆到盒心（重叠最大但几何糊成一团）。
    float interlock = 0.35f;
    // 仅 Pinwheel：每只龙在自身方位上的自转。0 = 径向（头朝盒心的辐射星形），
    // 90 = 切向（首尾相衔咬成一环）。径向时正对相机的那只只剩一小块横截面，
    // 偏向切向能让三只龙的投影面积均衡，咬合也更充分。
    float bladeDeg = 70.0f;
    // 仅 Pinwheel：整体相位角，转动整个风车以调整哪只龙朝向相机。
    float phaseDeg = 0.0f;
};

class Scene
{
public:
    Scene(TitusRHI::GpuModelHandle cornellHandle,
          const TitusMath::Mat4& cornellMatrix,
          std::vector<TitusMath::Vec3> cornellAlbedo,
          TitusRHI::GpuModelHandle dragonHandle,
          std::vector<DragonInstance> dragons);

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&& o) noexcept;
    Scene& operator=(Scene&& o) noexcept;
    ~Scene();

    TitusRHI::GpuModelHandle GetCornellHandle() const { return m_cornellHandle; }
    const TitusMath::Mat4& GetCornellMatrix() const { return m_cornellMatrix; }
    const std::vector<TitusMath::Vec3>& GetCornellAlbedo() const { return m_cornellAlbedo; }

    TitusRHI::GpuModelHandle GetDragonHandle() const { return m_dragonHandle; }
    const std::vector<DragonInstance>& GetDragons() const { return m_dragons; }
    std::vector<DragonInstance>& MutableDragons() { return m_dragons; }

    // 龙的局部 AABB，Fourier OIT 用它推算半透明几何的视空间深度窗口。
    const TitusMath::Vec3& GetDragonLocalMin() const { return m_dragonLocalMin; }
    const TitusMath::Vec3& GetDragonLocalMax() const { return m_dragonLocalMax; }

    // 布局计算需要的两组包围盒：龙的局部 AABB 与充当摆放区域的盒子内腔。
    void SetLayoutBounds(const TitusMath::Vec3& dragonLocalMin,
                         const TitusMath::Vec3& dragonLocalMax,
                         const TitusMath::Vec3& boxMin,
                         const TitusMath::Vec3& boxMax);

    // 按 params 重算全部实例的 modelMatrix / worldCenter；颜色保持不变。
    // 需先调用过 SetLayoutBounds，否则直接返回。
    void ApplyDragonLayout(const DragonLayoutParams& params);

private:
    void DestroyHandles();

    TitusRHI::GpuModelHandle m_cornellHandle{};
    TitusMath::Mat4 m_cornellMatrix{1.0f};
    std::vector<TitusMath::Vec3> m_cornellAlbedo;

    TitusRHI::GpuModelHandle m_dragonHandle{};
    std::vector<DragonInstance> m_dragons;

    bool m_hasLayoutBounds = false;
    TitusMath::Vec3 m_dragonLocalMin{0.0f};
    TitusMath::Vec3 m_dragonLocalMax{0.0f};
    TitusMath::Vec3 m_boxMin{0.0f};
    TitusMath::Vec3 m_boxMax{0.0f};
};

// 合并模型所有 SubMesh 的 AABB；无效时返回 false。
bool ComputeModelAabb(const TitusAsset::ModelAssetData& asset,
                      TitusMath::Vec3& outMin,
                      TitusMath::Vec3& outMax);

// 逐 SubMesh 取 Cornell 的反照率。McGuire 版 MTL 的 Kd 就是 Cornell 实测反射率
// （左墙红 0.63/0.065/0.05，右墙绿 0.14/0.45/0.091，其余纸白），直接用即可；
// 只有自发光顶灯要单独兜一个亮色。
std::vector<TitusMath::Vec3> MakeCornellAlbedo(const TitusAsset::ModelAssetData& asset);

// 沿盒子 X 轴并排放 count 只，朝向相同（只做均匀缩放 + 平移），贴地、z 居中。
std::vector<TitusMath::Mat4> MakeDragonRowTransforms(const TitusMath::Vec3& localMin,
                                                     const TitusMath::Vec3& localMax,
                                                     const TitusMath::Vec3& targetMin,
                                                     const TitusMath::Vec3& targetMax,
                                                     int count,
                                                     float heightFill);

// 风车咬合：count 只龙绕盒子中心的 Y 轴均分一圈，每只再自转 bladeDeg 成为斜置
// 的叶片，彼此插进相邻者的身体，形成 A 挡 B、B 挡 C、C 挡 A 的循环。
//
// 龙的局部 AABB 约 0.45 x 0.71 x 1.0，Z 向最长 —— 它本质上是一根沿 Z 躺着的
// 棍子，正好是"三棍互搭"需要的形状。因为龙有体积、彼此真实互穿，任意两只之间
// 就已经不存在"谁在前"的正确答案，per-object 排序在这个构型下必然失效。
//
// 尺寸取舍：设 halfZ 为龙 Z 向半长、boxRadius 为盒子内腔水平半径的 90%。先按
// heightFill 定统一缩放 s，并保证 s*halfZ 不超过 boxRadius 的 80%（给摆放半径
// 留出余量）；随后 r_max = boxRadius - s*halfZ 即龙中心能外推的最大半径，
// 实际半径 r = r_max*(1-interlock)。这个语义对任意 bladeDeg 都成立。
std::vector<TitusMath::Mat4> MakeDragonPinwheelTransforms(const TitusMath::Vec3& localMin,
                                                          const TitusMath::Vec3& localMax,
                                                          const TitusMath::Vec3& targetMin,
                                                          const TitusMath::Vec3& targetMax,
                                                          int count,
                                                          float heightFill,
                                                          float interlock,
                                                          float bladeDeg,
                                                          float phaseDeg);

// 第 i 只龙的默认颜色（青 / 品红 / 金，循环），和 Cornell 红绿墙错开。
TitusMath::Vec3 DefaultDragonAlbedo(int index);
