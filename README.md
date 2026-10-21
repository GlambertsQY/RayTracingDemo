# RayTracingDemo

基于 **Vulkan 硬件光追** 的实时路径追踪 Demo，使用 ImGui 构建界面，支持程序化场景与 glTF 室内场景，逐帧累积收敛。

| 程序化场景 | Khronos Sponza |
| :---: | :---: |
| ![程序化场景](docs/images/procedural.png) | ![Sponza](docs/images/sponza.png) |

## 特性

- **硬件光追路径追踪**：`VK_KHR_ray_tracing_pipeline` + `VK_KHR_acceleration_structure`，BLAS/TLAS + Shader Binding Table
- **迭代式弹射**：单 ray payload 内完成多跳（默认 4 跳），避免递归着色器与多 payload 开销
- **NEE 直接光照**：每跳对解析光源做一次采样，配合 GGX 重要性采样与余弦加权漫反射
- **材质系统**：metallic-roughness、法线贴图、Fresnel 玻璃、自发光、alpha mask
- **阴影射线**：使用 `rayQueryEXT`（`gl_RayFlagsOpaqueEXT`）做可见性测试，避免阴影 miss 着色器笔触
- **逐帧累积**：RGBA16F 辐射度 + RGBA32F 累积缓冲，compute shader 累加，相机移动时自动重置
- **降噪处理**：俄罗斯轮盘赌 + firefly clamp，ACES 色调映射 + gamma 校正
- **双场景**：程序化 ~500 实例波形阵列房间 / Sponza 室内 glTF 场景
- **RenderDoc 集成**：内置 In-Application API，可直接在程序内触发 `.rdc` 抓帧

## 渲染管线

每帧依次执行：

| 阶段 | 说明 |
| --- | --- |
| Ray Tracing | `path_tracer.rgen` 发射主射线，命中后执行迭代弹射与 NEE；`rayQuery` 做阴影测试 |
| Accumulate | `accumulate.comp` 把当前帧辐射度累加进历史缓冲 |
| Tonemap | `tonemap.frag` 采样累积结果，ACES 映射后输出到交换链 |
| UI | ImGui 覆盖层在同一个 render pass 内绘制 |

描述符集（set 0）：

| Binding | 资源 |
| --- | --- |
| 0 | TLAS |
| 1 | FrameConstants (UBO) |
| 2 / 3 | 全局顶点 / 索引缓冲 |
| 4 / 5 / 6 / 7 | Mesh / Instance / Material / Light SSBO |
| 8 | 纹理数组（128 层） |
| 9 / 10 | Radiance (RGBA16F) / Accumulation (RGBA32F) storage image |

## 依赖

所有第三方库均以 **git submodule** 引入并锁定版本：

| 库 | 版本 / commit |
| --- | --- |
| [imgui](https://github.com/ocornut/imgui) | `d88f238d` (v1.93.0-docking) |
| [glfw](https://github.com/glfw/glfw) | `3.5.1` |
| [volk](https://github.com/zeux/volk) | `vulkan-sdk-1.4.357.0` |
| [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | `3aa9212` |
| [glm](https://github.com/g-truc/glm) | `8d1fd52e` |
| [stb](https://github.com/nothings/stb) | `a7d4c217` |
| [tinygltf](https://github.com/syoyo/tinygltf) | `488a70a` |

## 构建

前置条件：

- Windows 10/11 + 支持硬件光追的 GPU（NVIDIA RTX / AMD RX 6000+ / Intel Arc）
- Vulkan SDK 1.4.x（需包含 `glslc`，CMake 会从 `%VULKAN_SDK%/Bin` 查找）
- Visual Studio 2026（生成器 `Visual Studio 18 2026`）或 CMake 3.28+

```powershell
git clone --recurse-submodules https://github.com/GlambertsQY/RayTracingDemo.git
cd RayTracingDemo

cmake --preset vs2026
cmake --build --preset release
```

生成的可执行文件位于 `build/Release/RayTracingDemo.exe`，编译后的 SPIR-V 会自动复制到同目录 `shaders/` 下。也可以直接打开 `build/RayTracingDemo.sln`。

## 场景资产

Sponza 模型不随仓库分发，首次运行前请执行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/fetch_assets.ps1
# 或
cmake --build build --target fetch_assets
```

脚本从 KhronosGroup/glTF-Sample-Assets 下载约 50 MB 贴图与 glTF 到 `assets/Sponza/`。若未下载，`--scene sponza` 会回退到程序化场景。

## 运行

```powershell
build\Release\RayTracingDemo.exe
build\Release\RayTracingDemo.exe --scene sponza --width 1600 --height 900
```

| 参数 | 说明 |
| --- | --- |
| `--scene <procedural\|sponza>` | 选择场景，默认 `procedural` |
| `--width <n>` / `--height <n>` | 初始窗口尺寸 |
| `--frames <n>` | 渲染指定帧数后自动退出（配合截图/抓帧使用） |
| `--screenshot <path>` | 退出前保存当前帧 PNG |
| `--renderdoc-capture` | 在第 3 帧触发 RenderDoc 抓帧 |

操作：`左键` 轨道旋转、`右键` 平移、`滚轮` 缩放、`F1` 显示/隐藏 UI。UI 面板可实时调整弹射次数、每帧采样数、曝光、太阳方位/高度、环境光强度、firefly clamp 等。

## RenderDoc 抓帧

启用 `--renderdoc-capture` 后，程序通过 `RENDERDOC_GetAPI` 在第 3 帧自动开始/结束抓帧，输出到工作目录下的 `captures/rdoc_capture.rdc`。该功能需要：

- 安装 RenderDoc（CMake 自动探测 `renderdoc_app.h`，找不到则编译期关闭，源码中由 `RTDEMO_HAVE_RENDERDOC` 控制）
- 使用 RenderDoc 的 `qrenderdoc` 或 MCP 服务打开 `.rdc` 分析各 Pass 耗时与资源绑定

![RenderDoc 抓帧](docs/images/renderdoc.png)

## 已知限制

- 累积渲染在低采样数下仍有轻微噪点，需要多帧收敛
- 阴影射线使用 `rayQuery` 并标记为 opaque，alpha mask 材质在阴影中按不透明处理
- 仅支持 Windows / MSVC 工具链，未做跨平台验证
