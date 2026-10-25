# AEG Engine

基于 **C++23、Vulkan 1.2、Dear ImGui** 的 Windows 桌面模型查看器，也是一个可继续扩展的引擎起点。CPU 模型导入、轨道相机、Vulkan 渲染器和编辑器界面分开组织；默认直接打开随程序附带的演示模型。

## 环境要求

- Windows 10 / 11 x64，以及支持 Vulkan 1.2 的显卡和驱动。
- Visual Studio 2026，安装“使用 C++ 的桌面开发”、MSVC x64 工具和 Windows SDK；同时勾选“用于 Windows 的 C++ CMake 工具”。构建脚本通过 `vswhere` 查找安装位置。
- PowerShell 7，以及 CMake 3.25 或更新版本、Ninja。脚本优先使用 Visual Studio 随附的 CMake / Ninja，无须修改系统 PATH。
- Vulkan SDK，包含头文件、链接库和 `glslc.exe`。设置 `VULKAN_SDK`；本机已有的 `E:\VulkanSDK\1.4.357.0` 可由脚本自动识别。
- 首次配置需要联网下载固定版本的第三方源码；运行程序不需要联网下载模型。

本机已检测工具：Visual Studio 2026 Community / MSVC 19.51、CMake 4.3.1、Vulkan SDK 1.4.357.0。构建脚本只给子进程设置编译环境，不修改用户或系统环境变量。

CLion 也可以使用自带的 MinGW 工具链；在 CMake 配置中选择对应工具链后重新加载项目即可。项目会修正 Assimp 5.4.3 传递的强制静态 pthread 链接参数，并将工具链的 `libwinpthread-1.dll` 复制到可执行文件旁。分发 MinGW 版本时请保留这个 DLL。MSVC 和 MinGW 请使用不同的构建目录。

## 构建与测试

在项目根目录打开 PowerShell 7：

```powershell
# Debug：配置、构建并运行 CPU 测试
.\scripts\build.ps1 -Configuration Debug

# Release：同样配置、构建并测试
.\scripts\build.ps1 -Configuration Release

# 显式启用真实 GPU 冒烟测试，需要可用桌面与 Vulkan 显卡
.\scripts\build.ps1 -Configuration Debug -GpuTests

# 只配置；或只构建已配置目录里的 CPU 测试目标
.\scripts\build.ps1 -Configuration Debug -ConfigureOnly
.\scripts\build.ps1 -Configuration Debug -SkipConfigure -Target aeg_core_tests -NoTests
```

可用 `-Jobs 4` 控制并行编译数，`-NoTests` 跳过测试。`-SkipConfigure` 使用现有缓存，也保留现有 GPU 测试设置；改变设置时应重新配置。普通构建默认关闭 GPU 测试。

如果已打开 Visual Studio 的 **x64 Native Tools Command Prompt / Developer PowerShell**，且 CMake、Ninja、Vulkan SDK 可用，也可以直接使用预设：

```powershell
cmake --preset debug
cmake --build --preset debug --parallel 8
ctest --preset debug

cmake --preset release
cmake --build --preset release --parallel 8
ctest --preset release
```

输出在 `build/debug/bin/` 或 `build/release/bin/`。构建会通过 `glslc --target-env=vulkan1.2` 编译 GLSL，并把 `shaders/*.spv`、`assets/` 复制到可执行文件旁。分发时保留整个 `bin` 目录；程序从可执行文件位置查找资源，允许从其他工作目录启动。

## 启动与命令行

```powershell
.\build\debug\bin\aeg_editor.exe
.\build\debug\bin\aeg_editor.exe --model 'D:\Models\example.glb'
.\build\debug\bin\aeg_editor.exe --model 'D:\Models\character\model.pmx'
.\build\debug\bin\aeg_editor.exe --font 'D:\Fonts\NotoSansSC-Regular.ttf'
.\build\debug\bin\aeg_editor.exe --msaa 4 --fxaa
.\build\debug\bin\aeg_editor.exe --taa
.\build\debug\bin\aeg_editor.exe --frames 12 --capture '.\build\viewport.ppm'
.\build\debug\bin\aeg_editor.exe --no-validation
```

`--model PATH` 选择 OBJ、glTF、GLB 或 PMX；`--font PATH` 指定支持中文的 TTF、OTF 或 TTC 字体；`--msaa 1|2|4|8` 选择场景采样数，1 为关闭；`--taa` 开启 TAA，启用时替代 MSAA；`--fxaa` 开启 FXAA 后处理，可跟随 MSAA 或 TAA；`--frames N` 在指定帧数后退出；`--capture PATH` 导出场景视口的 PPM 图像；`--hidden` 使用隐藏窗口，适合配合 `--frames` 自动验证；`--no-validation` 关闭 Vulkan 验证层。默认尝试启用验证层，未安装时仍可启动。参数缺失或未知参数会报告错误。

## 使用方法

- **导入**：在界面的模型路径栏输入文件路径并点击导入，或把模型文件拖入窗口。模型附带的相对纹理路径会相对模型文件解析。导入失败会保留当前模型并显示错误。
- **观察**：在场景视口内拖动鼠标旋转相机，水平拖动时可见模型表面跟随鼠标方向；使用中键拖动平移，滚轮缩放；点击“聚焦模型”或按 `F` 重新聚焦模型。具体拖动按键以界面底部提示为准。
- **调整**：在属性面板修改模型位置、旋转、缩放，以及光照方向、环境光、背景色等；可查看网格、顶点和三角形数量。
- **抗锯齿**：在属性面板的“抗锯齿”中选择 MSAA 关闭、2x、4x 或 8x，或开启“TAA 时域抗锯齿”；“FXAA 后处理”可独立开关。TAA 开启时有效采样数为 1，MSAA 选择禁用。界面显示历史帧数并提供“重置历史”；聚焦模型也会自动重置。界面只提供设备支持的采样档位，命令行请求不支持的 MSAA 档位时向下降级并显示提示。默认均关闭。
- **布局**：拖动 ImGui 面板标题可重新停靠；窗口与视口可调整尺寸。

## 中文界面与字体

编辑器面板、操作提示、状态和命令行帮助使用简体中文。模型名、网格名与路径按 UTF-8 显示，路径栏支持中文文本输入和粘贴。MSVC 与 MinGW 均启用 ImGui 的 Win32 IME 集成。

字体按以下顺序选择，路径可以包含中文和空格：

1. `--font PATH` 显式指定的字体。
2. 程序旁 `assets/fonts/` 中的 `NotoSansSC-Regular.ttf`、`NotoSansCJKsc-Regular.otf`、`SourceHanSansSC-Regular.otf`。
3. Windows 字体目录中的微软雅黑、微软正黑体、黑体或宋体。

默认不需要手动配置；没有可用中文字体时，程序会提示使用 `--font`。显式字体缺失、格式无效或不含中文字符时会报告错误。仓库不复制或分发 Windows 系统字体；如随程序附带其他字体，请遵守该字体的授权。

当前字库范围包含基本拉丁文、中日韩标点、假名、基本汉字及 CJK 扩展 A；实际字形取决于所选字体，不覆盖所有 Unicode 扩展区。已在真实窗口验证中文路径输入、复制粘贴、导入及名称显示；当前测试环境未验证拼音输入法的组合文本和候选窗。

## 抗锯齿渲染

MSAA 使用匹配采样数的场景颜色与深度附件，再解析到单采样场景图像；采样能力同时查询设备上限及实际颜色/深度格式。FXAA 在场景解析之后，根据感知亮度检测边缘并做有界方向搜索及亚像素过滤。所有抗锯齿均只作用于模型视口，ImGui 界面和文字保持清晰。

FXAA 保持线性 RGB 输出，使用线性过滤和边缘钳制；平坦区域直接保留原色。它属于空间近似抗锯齿，细节处可能略有柔化。截图导出的是最终抗锯齿结果。切换或缩放时先创建完整的新视口资源，成功后再替换，失败时保留此前有效设置和资源。

TAA 使用 8 相位 Halton 子像素抖动、排除抖动的相机/模型运动矢量、深度历史校验、3×3 邻域约束，以及双缓冲历史颜色/深度累积。历史权重在预热后最多为 0.9。它使用单采样场景替代 MSAA，随后可以再做 FXAA；TAA 单独开启时，截图会读取最终的 RGBA16F 历史颜色并转换为 PPM。

模型成功替换、视口缩放、抗锯齿切换、光照/背景变化、投影类型切换和聚焦模型会清空历史。普通相机环绕、平移、缩放及模型变换通过运动重投影继续累积。API `resetTemporalHistory()` 可在其他相机跳切时显式重置，且保留已经显示的最后一帧。仅成功提交的 GPU 帧才推进历史与抖动序列。

当前为基础 TAA，可能使细节略软，极细物体和复杂遮挡仍可能有残影；没有时域超分辨率、锐化或响应性遮罩。运动信息覆盖当前静态模型的整体变换，相应骨骼/变形动画仍未实现。所需浮点纹理格式不支持时，TAA 控件禁用，其他抗锯齿仍可用。

实现参考 [Vulkan 图像格式采样能力](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageFormatProperties.html) 和 [NVIDIA FXAA 白皮书](https://developer.download.nvidia.com/assets/gamedev/files/sdk/11/FXAA_WhitePaper.pdf)。

TAA 的累积、重投影与历史校验参考 [NVIDIA 的时域抗锯齿综述](https://research.nvidia.com/labs/rtr/publication/yang2020survey/)。

## 范围与限制

当前里程碑支持静态 OBJ / glTF / GLB / PMX 网格、节点变换、基础颜色、常见基础颜色纹理、深度测试和简单方向光照。纹理缺失或无法解码时使用白色回退并记录警告。

PMX 支持 UTF-8 / UTF-16LE、多个材质分组、基础 UV 贴图和中文相对贴图路径。导入时请保留模型配套的贴图目录；模型路径栏、拖放及 `--model` 共用导入器。显示的是静态初始姿态；没有 VMD 动作、骨骼蒙皮、Morph、刚体/关节模拟、描边、Toon 或球面贴图着色。材质与贴图透明度按阈值 0.1 做裁切近似，零透明度材质隐藏，半透明混合尚未实现，界面会显示相应导入提示。已验证 PMX 2.0；PMX 2.1 的部分 Morph 和软体等扩展受当前 Assimp 实现限制。

本版本主要面向不透明材质与基本裁切材质；没有完整 PBR、混合透明、骨骼动画、变形动画、物理、脚本、场景保存或生产级大模型流式加载。模型资产仍由使用者提供并遵守其授权；仓库演示模型可直接离线使用。

glTF 的 OPAQUE 会忽略 alpha，MASK 使用模型指定的 cutoff（默认 0.5）；BLEND 暂按不透明显示并给出导入提示。glTF 外部缓冲和纹理的 URI 支持百分号编码、空格与 UTF-8 名称。

## 已验证的里程碑

2026-10-25 在本机 MSVC 19.51 / RTX 5070 / Vulkan SDK 1.4.357.0 上完成 Debug、Release 编译。模块拆分后的完整 CTest **20/20 通过**，包含公开接口、相机、模型导入、字体四个 CPU 目标，以及 **16 项真实 GPU 测试**。原有 **33 项模型/相机用例及 8 项字体用例**保留，新增公开接口编译与运行检查。MinGW 构建和四个 CPU 目标也通过验证。CPU 回归覆盖左右拖动时模型表面的投影运动方向，以及 PMX 编码、坐标/绕序、UV、材质、中文路径、透明裁切和错误输入；字体回归检查中文实际字形、Unicode 字体路径、字体数据所有权和无效字体诊断；GPU 测试另检查 PMX 场景确实可见。

GPU 测试检查场景截图确实包含模型，覆盖 OPAQUE/MASK 材质、含空格及中文资源 URI、窗口与视口反复缩放、导入替换失败后画面保持不变，以及 ImGui 初始化失败时的资源回收。测试同时检查 Vulkan 同步验证及退出阶段的错误输出。测试使用隐藏窗口；`-GpuTests` 仍需要可用的 Windows 桌面和 Vulkan 驱动。

抗锯齿 GPU 回归检查斜边的实际像素覆盖、纯色和模型内部颜色、MSAA/FXAA 叠加后的最终截图、恢复关闭后的像素一致性，以及单像素/奇数尺寸、窗口缩放、重复切换、错误参数和模型替换。当前 RTX 5070 支持全部四档；缺少部分采样档位的物理设备尚未实测。MinGW 命令行已另验证所有档位叠加 FXAA 的真实渲染，Vulkan 验证错误均为零。

TAA GPU 回归验证静态多帧累积与帧间波动、正交及透视相机/模型运动的纹理重投影、重置确定性、遮挡变化、最终 FXAA 截图及单像素视口。测试中临时将运动矢量置零会使纹理滞后断言失败，恢复正确着色器后通过。Release 和 MinGW 均另完成 32 帧 TAA 渲染与截图检查，验证错误为零。真实窗口检查了 TAA/FXAA 开关、历史计数、重置、旋转和聚焦；运动观感使用间隔截图检查，未做逐帧视频分析。

## 代码结构与依赖

| 路径 | CMake 目标 | 命名空间 / 职责 |
| --- | --- | --- |
| `engine/foundation/` | `aeg::foundation` | `aeg::foundation`，Bounds、UTF-8 路径处理 |
| `engine/camera/` | `aeg::camera` | `aeg::camera`，轨道相机 |
| `engine/assets/` | `aeg::assets` | `aeg::assets`，CPU 模型数据、Assimp 导入、纹理解码、PMX 校验 |
| `engine/render/` | `aeg::render` | `aeg::render`，Vulkan 设备、交换链、场景、视口、抗锯齿、截图 |
| `editor/` | `aeg::editor` | `aeg::editor`，窗口、主循环、停靠、各面板与字体 |
| `apps/editor/private/` | `aeg_editor` | 全局 main、`aeg::app` 命令行入口 |
| `tests/{foundation,camera,assets,render,editor}/` | 对应测试目标 | 与功能目录对应的 CPU/GPU 回归；数据保留在 `tests/fixtures/` |
| `assets/` | 运行时复制 | 离线演示模型与纹理 |

各模块都有自己的 `CMakeLists.txt`。公开头文件直接位于 `public/`，内部头文件与源文件位于 `private/`。例如：

```text
engine/render/
├─ CMakeLists.txt
├─ public/
│  ├─ VulkanRenderer.hpp
│  ├─ RenderSettings.hpp
│  └─ AntialiasingSettings.hpp
└─ private/
   ├─ VulkanRenderer.cpp
   ├─ VulkanRendererImpl.hpp
   ├─ vulkan/
   ├─ scene/
   ├─ viewport/
   ├─ antialiasing/
   └─ shaders/{scene,postprocess}/
```

`VulkanRenderer.cpp` 保留初始化、帧提交和销毁的协调；设备、交换链、资源、管线、场景、视口和各抗锯齿通道的实现分别位于对应文件。GLSL 也随渲染模块组织，编译后的运行时文件名和目录保持一致。

使用模块时链接其 CMake 目标，通过扁平头文件名引用公开接口：

```cmake
target_link_libraries(my_tool PRIVATE aeg::camera aeg::render)
```

```cpp
#include <OrbitCamera.hpp>
#include <ModelImporter.hpp>
#include <VulkanRenderer.hpp>

aeg::camera::OrbitCamera camera;
auto model = aeg::assets::loadModel(path);
```

CMake 仅导出各模块的 `public/`；`private/` 只供模块自身使用。公开接口测试不获得任何模块的 private 搜索路径，9 个公开头文件也分别通过独立编译检查。字体白盒测试单独获得 `editor/private/fonts/`，该目录不会传递给应用程序。GPU 测试链接同一个渲染库，统一复用其实现。`aeg_core_tests` 保留为相机与模型导入测试的构建聚合目标。

重构前后相同输入的 TAA 与 MSAA+FXAA 场景截图逐字节一致；导入、相机与抗锯齿行为继续通过现有回归验证。

固定依赖版本及 SHA256 校验在 `cmake/Dependencies.cmake`：

| 依赖 | 固定版本 | 官方来源 |
| --- | --- | --- |
| GLFW | 3.4 | [glfw/glfw](https://github.com/glfw/glfw/releases/tag/3.4) |
| GLM | 1.0.1 | [g-truc/glm](https://github.com/g-truc/glm/releases/tag/1.0.1) |
| Assimp | v5.4.3，OBJ / glTF / MMD(PMX) 导入器，静态库、内置 zlib | [assimp/assimp](https://github.com/assimp/assimp/releases/tag/v5.4.3) |
| Dear ImGui | v1.91.9b-docking，GLFW / Vulkan 后端 | [ocornut/imgui](https://github.com/ocornut/imgui/releases/tag/v1.91.9b-docking) |
| stb_image | commit `003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5` | [nothings/stb](https://github.com/nothings/stb/commit/003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5) |

依赖源码缓存于 `build/_deps/*-src`，Debug 与 Release 使用独立编译目录。第三方许可证保留在各依赖源码目录中；发布二进制时需同时提供相应许可证。

项目通过 `cmake/ApplyAssimpMmdPatch.cmake` 自动应用针对固定版本的 MMD 修正：附加 UV 索引以及场景转换前的网格、材质、贴图和骨骼引用检查。补丁有明确源码锚点并可重复配置；更换 Assimp 版本时需要一并检查补丁。回归 PMX 均由 `tests/fixtures/generate_pmx.py` 生成，为项目原创小模型。

若出现 Vulkan 初始化错误，先检查显卡驱动与 SDK；验证层诊断输出在终端。若编译器或 SDK 路径改变，可删除对应的 `build/debug` 或 `build/release` 目录后重新配置，保留共享源码缓存即可。
