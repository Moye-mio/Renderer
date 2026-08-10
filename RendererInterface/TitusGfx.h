#pragma once
// ============================================================================
// RendererInterface - TitusGfx.h
// 唯一的对外门面头文件。外部模块只 include 这一个文件，**不**得直接 include
// Renderer/、RendererCore/、RendererVK/ 任何头。
//
// 命名空间分组（与旧 Renderer/Interface.h 的 TitusGraphics::* 对齐）：
//   APP / WINDOW_KEYWORD / COMPONENT_CONFIG / RESOURCE_MANAGER / INPUT_MANAGER / CAMERA
//
// 后端选择：
//   - 通过命令行参数 `--backend=gl|vk|null` 选择
//   - 或调用 APP::SetBackend(GBackend) 显式设置
// 线程模式：
//   - 通过命令行参数 `--threading=direct|threaded|nonthreaded`
//   - 或调用 APP::SetThreadingMode(GThreadingMode) 显式设置
// Validation（仅 Vulkan 生效）：
//   - 通过命令行参数 `--validation=on|off`（亦接受 true/false/1/0）
//   - 或调用 APP::SetEnableValidation(bool) 显式设置
//   - 未指定时：Debug 默认 on，Release 默认 off
// ============================================================================
#include <cstdint>
#include <memory>
#include <string>
#include <any>
#include <type_traits>

#include "Basic/TitusMath.h"

// GBackend / GThreadingMode 的权威定义在 RendererCore 中，但业务模块
// 不应直接 include "RendererCore/*.h"；这里通过 RendererInterface 自身的
// 转发头 TitusGfxEnums.h 把这两个 enum-only 头"上行"暴露出去——业务工程
// 的 AdditionalIncludeDirectories 只需要包含 $(SolutionDir) 即可正确解析
// 下方所有 "模块/头文件.h" 形式的 include。
//
// 注意：IRenderPass 在 RendererCore 中定义；门面 API（AddPass）只需要
// 不完整类型即可声明形参，因此本头仅做前向声明。如果业务代码确实需要
// 继承 IRenderPass（或调用其方法），应当 #include "TitusGfxPass.h"，
// 这是项目中"暴露 RendererCore 实现侧 API"的唯一入口。
#include "TitusGfxEnums.h"

// GHandle 轻量头：业务侧 SharedData 白名单需要识别 TextureHandle /
// BufferHandle / SamplerHandle 等门面句柄类型。GHandle.h 不依赖任何
// 后端模块，仅含 6 个 Tag + GHandle<Tag> 模板。
#include "RendererCore/GHandle.h"

// ----------------------------------------------------------------------------
// 资产中间表示（IR）前向声明：UploadGpuModel 的形参类型来自 AssetLoader。
// 业务侧若需要传入 ModelAssetData，需要 #include "AssetLoader/AssetTypes.h"
// 以拿到完整定义；门面头本身只持前向声明，避免把 AssetTypes.h 塞给所有
// 不需要资产类型的业务编译单元。
// ----------------------------------------------------------------------------
namespace TitusAsset
{
    struct ModelAssetData;
}

namespace TitusRHI
{
    // 前向声明：完整定义见 RendererCore/IRenderPass.h，由 TitusGfxPass.h 转发
    class IRenderPass;

    // ------------------------------------------------------------------------
    // GpuModelHandle —— RendererInterface 自有的不透明模型句柄。
    //   - 业务侧通过 APP::UploadGpuModel(ModelAssetData) 拿到；
    //   - 内部由 TitusGfx.cpp 的注册表把 id 映射到 RendererCore::GpuModel；
    //   - 与其他 GHandle 不同，本 Tag 不暴露到 RendererCore，而是仅在
    //     RendererInterface 内部使用，避免污染底层抽象。
    // ------------------------------------------------------------------------
    struct GpuModelTag
    {
    };

    struct GpuModelHandle
    {
        uint64_t id = 0;
        constexpr GpuModelHandle() = default;

        constexpr explicit GpuModelHandle(uint64_t v) : id(v)
        {
        }

        constexpr bool IsValid() const { return id != 0; }
        constexpr bool operator==(GpuModelHandle o) const { return id == o.id; }
        constexpr bool operator!=(GpuModelHandle o) const { return id != o.id; }
    };

    // ------------------------------------------------------------------------
    // APP —— 应用生命周期 + 后端 / 线程选择
    // ------------------------------------------------------------------------
    namespace APP
    {
        // 后端 / 线程模式 / Validation 选择（必须在 InitApp 之前调用；命令行解析也写到本组）
        void SetBackend(GBackend backend);
        void SetThreadingMode(GThreadingMode mode);
        void SetEnableValidation(bool enable);
        GBackend GetBackend();
        GThreadingMode GetThreadingMode();
        bool GetEnableValidation();

        // 命令行解析：识别 --backend=gl|vk|null、--threading=direct|threaded|nonthreaded、
        // --validation=on|off（亦接受 true/false/1/0）
        // 解析后会写入 SetBackend / SetThreadingMode / SetEnableValidation
        void ParseCommandLine(int argc, char** argv);

        // 应用生命周期
        // InitApp 内部会依次完成：创建窗口 → 创建 GDevice →
        // device.Init(window) → 创建 PassScheduler → 调用 InitAllPasses。
        void InitApp();
        // UpdateApp 主循环：PollEvents → PassScheduler::DrawFrame → SwapBuffers。
        void UpdateApp();
        // ShutdownApp 逆序释放：DestroyAllPasses → device.Shutdown → window.Shutdown。
        void ShutdownApp();

        // -- 主循环辅助 --
        // 是否已收到窗口关闭事件（InitApp 之后调用才有意义）。
        bool ShouldClose();
        // 同步等待 GPU 空闲（退出前调用，避免 in-flight 资源被提前销毁）。
        void WaitIdle();

        // -- Pass 注册 --
        // 调用者需额外 include "RendererInterface/TitusGfxPass.h" 以获得
        // TitusRHI::IRenderPass 的完整声明（实际上在 RendererCore 中定义）。
        // AddPass 后接管生命周期：InitAllPasses / DestroyAllPasses 由门面在
        // 适当时机隐式调用（InitApp 后 + ShutdownApp 前）。
        void AddPass(std::shared_ptr<IRenderPass> pass);

        // -- GpuModel 上传 / 销毁（资产分层）--
        // 输入是 AssetLoader 解码后的 CPU IR（const TitusAsset::ModelAssetData&）；
        // gfx 模块**不接受 path 形参，也不在内部做任何磁盘 IO**。
        // 必须在 InitApp 之后调用（依赖 device 存在）；返回 0 句柄表示失败。
        // DestroyGpuModel 可在 ShutdownApp 之前任意时机调用；之后该 handle
        // 失效，再使用属于未定义行为。
        GpuModelHandle UploadGpuModel(const TitusAsset::ModelAssetData& asset);
        void DestroyGpuModel(GpuModelHandle handle);

        // -- 内部访问器：仅供 TitusGfxPass.h 中的 inline DrawGpuModel 使用 --
        // 业务侧不应直接调用；返回 const void* 是为了避免本头依赖 GpuModel
        // 完整定义。TitusGfxPass.h 中已经 include 了 GpuModel 完整头，可在那
        // 里把 void* 静态强转回 const GpuModel*。
        const void* GetGpuModelInternal(GpuModelHandle handle);

        // -- 跨后端单元测试入口 --
        // 通过 GDeviceHeadless 跑一遍设备生命周期 + 资源 + 帧循环 + 延迟销毁的
        // 完整流程；无需 GPU/窗口；返回值为失败用例数（0 表示全通过）。
        // 通常通过 `010_UnifiedTriangle --run-tests` 触发。
        int RunUnitTests();
    }

    // ------------------------------------------------------------------------
    // WINDOW_KEYWORD —— 窗口属性
    // ------------------------------------------------------------------------
    namespace WINDOW_KEYWORD
    {
        void SetWindowSize(int width, int height, bool isViewportSizeChangedWithWindow = true);
        void SetIsCursorDisable(bool isCursorDisable);
        int GetWindowWidth();
        int GetWindowHeight();
        void SetWindowTitle(const std::string& title);
    }

    // ------------------------------------------------------------------------
    // COMPONENT_CONFIG —— 组件开关
    // ------------------------------------------------------------------------
    namespace COMPONENT_CONFIG
    {
        void SetIsEnableGUI(bool isEnableGUI);
    }

    // ------------------------------------------------------------------------
    // RESOURCE_MANAGER —— 资源/Pass 注册 + 跨 Pass 共享数据黑板
    //   - 由于跨后端，这里仅暴露 Pass 注册与共享数据；具体 Pass 类型由外部
    //     使用 RendererCore::IRenderPass 派生（外部模块若需要继承 Pass，
    //     可单独 include "TitusGfxPass.h" 子头，详见 GetPassRegistry）
    //   - 共享数据黑板使用 std::any 内部存储，并通过 static_assert 限制 T
    //     仅为门面类型白名单，避免业务把后端原生类型（GLuint / VkBuffer / ...）
    //     存进黑板。
    // ------------------------------------------------------------------------
    namespace RESOURCE_MANAGER
    {
        // 透明指针（外部模块按 IRenderPass 形式继承使用，详见 TitusGfxPass.h）
        class IGRenderPass;

        void RegisterRenderPass(const std::shared_ptr<IGRenderPass>& pass);
        void RemoveAllPasses();

        // -- 共享数据黑板（内部实现）--
        namespace detail
        {
            // 类型白名单：业务能向黑板存入的所有合法 T 必须在此 OR 链中。
            // 增加新类型时同步更新本模板。
            template <typename T>
            struct IsAllowedSharedDataType
                : std::bool_constant<
                    std::is_same_v<T, int> ||
                    std::is_same_v<T, float> ||
                    std::is_same_v<T, TitusMath::Vec3> ||
                    std::is_same_v<T, TitusMath::Vec4> ||
                    std::is_same_v<T, TitusMath::Mat4> ||
                    std::is_same_v<T, TitusRHI::GpuModelHandle> ||
                    std::is_same_v<T, TitusRHI::TextureHandle> ||
                    std::is_same_v<T, TitusRHI::BufferHandle> ||
                    std::is_same_v<T, TitusRHI::SamplerHandle>
                >
            {
            };

            // 把任意 std::any 从内部存储中读出。失败时返回 nullptr。
            const std::any* FindSharedDataAny(const std::string& name);
            void StoreSharedDataAny(const std::string& name, std::any value);
        } // namespace detail

        // 业务可见的强类型 API（模板内联在头里以便编译期 static_assert）。
        template <typename T>
        void RegisterSharedData(const std::string& name, const T& value)
        {
            static_assert(detail::IsAllowedSharedDataType<T>::value,
                          "TitusRHI::RESOURCE_MANAGER::RegisterSharedData<T>: T must be one of "
                          "{int, float, TitusMath::Vec3, TitusMath::Vec4, TitusMath::Mat4, "
                          "GpuModelHandle, TextureHandle, BufferHandle, SamplerHandle}. "
                          "Backend-native types (GLuint / VkBuffer / shared_ptr<Texture> ...) "
                          "are NOT allowed across the facade boundary.");
            detail::StoreSharedDataAny(name, std::any(value));
        }

        // 失败语义：name 未注册或类型不匹配 → 返回 T{}（同时打印 stderr）。
        template <typename T>
        T GetSharedDataByName(const std::string& name)
        {
            static_assert(detail::IsAllowedSharedDataType<T>::value,
                          "TitusRHI::RESOURCE_MANAGER::GetSharedDataByName<T>: T must be in the "
                          "allowed type whitelist (see RegisterSharedData<T>).");
            const std::any* a = detail::FindSharedDataAny(name);
            if (!a) return T{};
            if (const T* p = std::any_cast<T>(a)) return *p;
            return T{};
        }
    }

    // ------------------------------------------------------------------------
    // INPUT_MANAGER —— 输入查询
    // 跨后端键鼠输入查询接口。键码 / 鼠标按钮码采用与 GLFW_KEY_* /
    // GLFW_MOUSE_BUTTON_* 一致的常量值（业务侧无需 include GLFW，可直接使用
    // 下方 KEY_* / MOUSE_BUTTON_* 常量）。
    // ------------------------------------------------------------------------
    namespace INPUT_MANAGER
    {
        // —— 键值常量（与 GLFW_KEY_* 数值对齐，业务侧无需 include GLFW） ——
        constexpr int KEY_W      = 87;
        constexpr int KEY_A      = 65;
        constexpr int KEY_S      = 83;
        constexpr int KEY_D      = 68;
        constexpr int KEY_Q      = 81;
        constexpr int KEY_E      = 69;
        constexpr int KEY_SPACE  = 32;
        constexpr int KEY_LSHIFT = 340;
        constexpr int KEY_LCTRL  = 341;
        constexpr int KEY_ESCAPE = 256;

        constexpr int MOUSE_BUTTON_LEFT   = 0;
        constexpr int MOUSE_BUTTON_RIGHT  = 1;
        constexpr int MOUSE_BUTTON_MIDDLE = 2;

        // 返回 1 表示按下（GLFW_PRESS / REPEAT），0 表示未按下。
        int GetKeyStatus(int key);

        // 返回鼠标按钮按下状态：1 表示按下，0 表示未按下。
        int GetMouseButton(int button);

        // 当前光标位置（窗口坐标系，左上为原点，Y 朝下）。
        // 第一次调用前未发生过鼠标事件时可能为窗口中心。
        void GetCursorPos(double& x, double& y);

        // 设置鼠标光标是否被禁用（捕获 + 隐藏，用于 FPS 风格相机）。
        void SetCursorDisabled(bool disabled);
        bool IsCursorDisabled();
    }

    // ------------------------------------------------------------------------
    // CAMERA —— 主相机访问
    // 业务侧只读这些值；写入由 RendererInterface 内部 CameraService 在主
    // 循环中根据窗口/输入事件驱动（尚未接入真实输入，先返回单位
    // 矩阵或由调用方通过 SetXxx 显式注入）。
    // ------------------------------------------------------------------------
    namespace CAMERA
    {
        double GetMainCameraFov();
        void SetMainCameraFov(double fov);

        // -- 主相机查询（业务侧只读） --
        TitusMath::Mat4 GetMainCameraViewMatrix();
        TitusMath::Mat4 GetMainCameraProjectionMatrix();
        TitusMath::Vec3 GetMainCameraPosition();

        // -- 主相机注入（供 RendererInterface 内部 / 上层应用代码驱动） --
        // 这三个 setter 不在"业务侧"被调用；保留是为了让上层 demo 在 GUI
        // 控件 / 鼠标键盘事件中显式驱动相机。
        void SetMainCameraViewMatrix(const TitusMath::Mat4& view);
        void SetMainCameraProjectionMatrix(const TitusMath::Mat4& proj);
        void SetMainCameraPosition(const TitusMath::Vec3& pos);

        // --------------------------------------------------------------------
        // 内置 FPS 飞行相机控制器
        //   - WASD：前后左右平移；Q/E：上下平移；LSHIFT 加速、LCTRL 减速
        //   - 鼠标右键拖拽：旋转视角（yaw/pitch）
        //   - EnableBuiltinFlyCamera(true) 后由 APP::UpdateApp 在每帧
        //     PollEvents 之后、DrawFrame 之前自动消费 INPUT_MANAGER 状态
        //     并刷新主相机 view/projection/position。
        //   - 业务仍可在 EnableBuiltinFlyCamera(false) 时完全自管相机；
        //     或在启用状态下通过 SetMainCameraViewMatrix 在帧内强制覆盖
        //     （但下一帧会被 FlyCamera 再覆盖回去，需先 Disable）。
        //   - aspect 在 WINDOW_KEYWORD::SetWindowSize 改变后会自动同步。
        // --------------------------------------------------------------------
        struct FlyCameraConfig
        {
            TitusMath::Vec3 position{0.0f, 0.0f, 4.0f};
            float yawDeg = -90.0f;   // -90 → 朝 -Z
            float pitchDeg = 0.0f;
            float fovDeg = 60.0f;
            float aspect = 0.0f;     // <=0 表示自动跟随窗口
            float nearPlane = 0.1f;
            float farPlane = 100.0f;
            float moveSpeed = 4.0f;          // 单位/秒
            float mouseSensitivity = 0.15f;  // 度/像素
            float fastMultiplier = 3.0f;     // LSHIFT 加速倍率
            float slowMultiplier = 0.3f;     // LCTRL 减速倍率
            int dragMouseButton = 1;         // INPUT_MANAGER::MOUSE_BUTTON_RIGHT
        };

        void EnableBuiltinFlyCamera(bool enable);
        bool IsBuiltinFlyCameraEnabled();
        void SetBuiltinFlyCameraConfig(const FlyCameraConfig& cfg);
        FlyCameraConfig GetBuiltinFlyCameraConfig();
    }
}
