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
.\scripts\build.ps1 -Configuration Debug -SkipConfigure -Target aeg_core_tests
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
.\build\debug\bin\aeg_editor.exe --frames 12 --capture '.\build\viewport.ppm'
.\build\debug\bin\aeg_editor.exe --no-validation
```

`--model PATH` 选择 OBJ、glTF、GLB 或 PMX；`--frames N` 在指定帧数后退出；`--capture PATH` 导出场景视口的 PPM 图像；`--hidden` 使用隐藏窗口，适合配合 `--frames` 自动验证；`--no-validation` 关闭 Vulkan 验证层。默认尝试启用验证层，未安装时仍可启动。参数缺失或未知参数会报告错误。

## 使用方法

- **导入**：在界面的模型路径栏输入文件路径并点击导入，或把模型文件拖入窗口。模型附带的相对纹理路径会相对模型文件解析。导入失败会保留当前模型并显示错误。
- **观察**：在场景视口内拖动鼠标旋转相机，水平拖动时可见模型表面跟随鼠标方向；使用中键拖动平移，滚轮缩放；点击“Frame Model”或按 `F` 重新聚焦模型。具体拖动按键以界面底部提示为准。
- **调整**：在属性面板修改模型位置、旋转、缩放，以及光照方向、环境光、背景色等；可查看网格、顶点和三角形数量。
- **布局**：拖动 ImGui 面板标题可重新停靠；窗口与视口可调整尺寸。

## 范围与限制

当前里程碑支持静态 OBJ / glTF / GLB / PMX 网格、节点变换、基础颜色、常见基础颜色纹理、深度测试和简单方向光照。纹理缺失或无法解码时使用白色回退并记录警告。

PMX 支持 UTF-8 / UTF-16LE、多个材质分组、基础 UV 贴图和中文相对贴图路径。导入时请保留模型配套的贴图目录；模型路径栏、拖放及 `--model` 共用导入器。显示的是静态初始姿态；没有 VMD 动作、骨骼蒙皮、Morph、刚体/关节模拟、描边、Toon 或球面贴图着色。材质与贴图透明度按阈值 0.1 做裁切近似，零透明度材质隐藏，半透明混合尚未实现，界面会显示相应导入提示。已验证 PMX 2.0；PMX 2.1 的部分 Morph 和软体等扩展受当前 Assimp 实现限制。

本版本主要面向不透明材质与基本裁切材质；没有完整 PBR、混合透明、骨骼动画、变形动画、物理、脚本、场景保存或生产级大模型流式加载。模型资产仍由使用者提供并遵守其授权；仓库演示模型可直接离线使用。

glTF 的 OPAQUE 会忽略 alpha，MASK 使用模型指定的 cutoff（默认 0.5）；BLEND 暂按不透明显示并给出导入提示。glTF 外部缓冲和纹理的 URI 支持百分号编码、空格与 UTF-8 名称。界面目前使用英文和 ImGui 默认字体；中文路径可以导入，但中文名称的显示可能出现缺字。

## 已验证的里程碑

2026-10-25 在本机 MSVC 19.51 / RTX 5070 / Vulkan SDK 1.4.357.0 上完成 Debug、Release 编译。完整 CTest **15/15 通过**，包含一个具有 **33 项 CPU 用例**的目标及 **14 项真实 GPU 测试**。CPU 回归覆盖左右拖动时模型表面的投影运动方向，以及 PMX 编码、坐标/绕序、UV、材质、中文路径、透明裁切和错误输入；GPU 测试另检查 PMX 场景确实可见。

GPU 测试检查场景截图确实包含模型，覆盖 OPAQUE/MASK 材质、含空格及中文资源 URI、窗口与视口反复缩放、导入替换失败后画面保持不变，以及 ImGui 初始化失败时的资源回收。测试同时检查 Vulkan 同步验证及退出阶段的错误输出。测试使用隐藏窗口；`-GpuTests` 仍需要可用的 Windows 桌面和 Vulkan 驱动。

## 代码结构与依赖

| 路径 / 目标 | 职责 |
| --- | --- |
| `include/aeg/` | 模块公共接口 |
| `src/assets/`、`src/core/` / `aeg_core` | Assimp 导入、纹理解码、CPU 数据、轨道相机 |
| `src/render/` | Vulkan 设备、资源、离屏视口、呈现与截图 |
| `src/editor/`、`src/main.cpp` / `aeg_editor` | 界面、交互、文件导入、命令行 |
| `shaders/`、`assets/` | GLSL 与离线演示资源 |
| `tests/` / `aeg_core_tests` | CPU 导入和相机回归测试；可选 GPU 截图验证 |

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
