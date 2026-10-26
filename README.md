# AEG Engine

基于 **C++23、Vulkan 1.2、Dear ImGui** 的 Windows 桌面模型查看器，也是一个可继续扩展的引擎起点。CPU 模型导入、轨道相机、通用 Renderer、RHI、Vulkan 后端和编辑器界面分开组织；默认直接打开随程序附带的演示模型。

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

输出在 `build/debug/bin/` 或 `build/release/bin/`。构建先通过 `glslc --target-env=vulkan1.2` 将 GLSL 编译到 `compiled-shaders/*.spv`，再提取为 `generated/embedded_shaders/*.spv.hpp` 中的 `constexpr std::uint32_t` 数组，编译进渲染库。运行时直接使用静态内存中的字节视图创建着色器，不打开或读取 SPIR-V 文件，也不需要分发 `shaders/` 目录。模型等 `assets/` 资源继续复制到可执行文件旁，程序允许从其他工作目录启动。

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
- **观察**：模型加载后默认从 +Z 沿 -Z 正视，重新导入也恢复该朝向。在场景视口内拖动鼠标旋转相机，水平拖动时可见模型表面跟随鼠标方向；使用中键拖动平移，滚轮缩放；点击“聚焦模型”或按 `F` 重新聚焦模型，保留当前观察角度。具体拖动按键以界面底部提示为准。
- **调整**：在属性面板修改模型位置、旋转、缩放，以及光照方向、环境光、背景色等；可查看网格、顶点和三角形数量。
- **法线贴图**：导入模型时自动读取材质声明的法线图，缺少声明时按文件名查找配套图片；所有已支持格式共用此规则。仍可在属性面板“材质”中选择材质，输入 RGB 法线图片路径，应用、替换或清除贴图；相对路径以模型所在目录为基准，也支持绝对路径和中文路径。“光照”中的“使用法线贴图”和 0–4 的“法线强度”控制全部材质，默认启用、强度为 1；强度为 0 时使用原来的顶点法线。无光照模式不受法线贴图影响，界面同时显示带法线贴图的材质数。
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

## 法线贴图渲染

法线贴图属于统一材质能力，适用于 OBJ、glTF、GLB、MMD/PMX 和程序生成网格。glTF / GLB 的 `normalTexture`（含内嵌图片）及 OBJ / MTL 的 `norm`、`map_Kn` 可从模型材质自动读取；PMX 还会读取同名 `.emd` 的材质效果绑定，通过 `.fx` 声明找到法线图片。没有明确绑定时才按文件名查找外部配套图片。所有格式保留同一个手动替换与清除入口，不把基础贴图、球面或 Toon 贴图误作法线图。

MMD 绑定顺序为 `.emd` 的 `[Effect] Obj[n]` → `.fx` 的 `USE_NORMALMAP` / `TEXTURE_NORMALMAP` → 图片。`Obj[n]` 使用从 0 开始的原材质索引；材质项覆盖 `Obj` 默认项，显式 `none` 保持该材质无效果，阻止文件名猜测。`.fx` 支持常见字符串宏、别名、基本条件和有界 `#include`；包含文件相对当前文件解析，图片相对入口 `.fx` 目录解析。反斜杠续行按词法拼接；存在优先级歧义的无括号复合条件宏会明确降级，避免选错图片。原生法线声明优先于 EMD，EMD 明确绑定优先于命名匹配。绑定文件缺失、图片损坏或声明不支持时会给出提示，保留顶点法线，可继续手动覆盖。

当前只提取上述法线资源声明，不编译或执行 MME/HLSL 效果，也不复现其 Toon、高光、描边、阴影、视差或多层法线。`NormalMapResolution` 是 UV 平铺参数，当前只支持默认值 1；其他值会提示不支持，不会误当成法线强度。中文路径、Unicode BOM 和旧式文本注释均可处理；对包含深度、文件数和大小设置了边界，遇到循环或超限会诊断回退。

自动匹配依次使用基础贴图完整文件名（不含扩展名）、去掉颜色后缀的名称、材质名，最后在只有一个使用中材质时尝试模型文件名。颜色后缀包括 `_base_color`、`_basecolor`、`_albedo`、`_diffuse`、`_color`、`_base`、`_d`。每一层找到候选后便停止向后尝试。常见配套文件名示例：

| 基础贴图 / 材质名 | 可自动匹配的法线图示例 |
| --- | --- |
| `衣.png` | `衣N.png`、`衣_n.png`、`衣_normal.png` |
| `Body_diffuse.png` | `Body_diffuse_n.png`，或下一层的 `Body_n.png` |
| 材质名 `Body` | `Body_normal.png`、`normal_Body.png`、`normalmap/Body.png` |

支持后缀 `_normal`、`_normals`、`_n`、`_nrm`、`_nor`、`_normalmap`、`_normal_map`、`normal`、`n`，以及前缀 `normal_`、`n_`；支持 PNG、JPG/JPEG、TGA、BMP。文件名中的 ASCII 字母忽略大小写，中文和字面 `%20` 保持原样。查找范围限于基础贴图目录、模型目录、模型的 `tex` / `texture` / `textures` 目录及上述位置的 `normal` / `normals` / `normalmap` / `normalmaps` 子目录；在明确的法线目录中还支持同名图片。不会递归扫描整个项目。

同一匹配层有多个文件时提示歧义，保留顶点法线，可手动指定；图片损坏时提示并回退；没有匹配时保持安静。模型明确声明的法线图优先，即使其图片缺失或 UV 不支持，也不会用猜测结果覆盖该声明。自动查找只在导入初始化时执行，手动清除后普通材质更新不会把图片重新加回来。没有路径信息的程序模型不会扫描当前工作目录。

法线数据按线性 UNORM 采样，基础颜色纹理继续按 sRGB 采样；glTF 的材质 `normalTexture.scale` 作用于切线空间 X/Y，并与编辑器的全局强度共同生效。切换法线开关、修改强度或替换材质贴图会重置 TAA 历史，也可与 MSAA、FXAA 组合使用。

切线基于节点变换后的最终网格和 UV0 生成，镜像 UV 的方向分开处理；模型后续的非均匀缩放与镜像变换也会相应变换切线基。缺失贴图、非 UV0 坐标、纹理 UV 变换或缺失 UV 会给出提示并回退到顶点法线；退化 UV/几何仅对受影响的三角形回退。当前生成切线的方法不保证与资产制作工具的 MikkTSpace 烘焙结果完全一致。

当前只支持 RGB 切线空间法线，不转换 OBJ 的 `bump` / `map_bump` 高度图。编辑器中的外部贴图绑定用于当前会话，尚未提供工程或材质配置保存。数据约定参考 [Khronos glTF 法线纹理规范](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materialnormaltexture)。

公开接口位于 `engine/assets/public/MaterialTextures.hpp`，图片解码、自动查找与材质准备的实现分别放在 `private/image/`、`private/material/`。格式导入器只需将网格、UV、外部基础贴图路径和原生法线声明映射到统一 `ModelData`，然后调用一次 `initializeModelMaterials()`；图片查找、解码、切线生成及 Vulkan 着色代码均可直接复用。`loadModel()` 已在所有格式导入完成后统一执行该步骤。`prepareModelMaterials()` 只准备现有材质，不重新自动查找；外部图片赋值接口也会执行相同准备，不需要识别模型扩展名：

```cpp
#include <MaterialTextures.hpp>
#include <ModelImporter.hpp>

auto model = aeg::assets::loadModel(modelPath);
aeg::assets::setMaterialNormalMap(model, materialIndex, normalImagePath);
renderer.uploadModel(model);
// 也可传入 ImageData；clearMaterialNormalMap(model, materialIndex) 清除贴图。
```

指定、替换或准备材质失败时保留原 CPU 数据；编辑器也会在 GPU 上传成功后再替换当前模型，失败时保留此前显示效果。

## 范围与限制

当前里程碑支持静态 OBJ / glTF / GLB / PMX 网格、节点变换、基础颜色、常见基础颜色纹理、RGB 法线贴图、深度测试和简单方向光照。基础颜色纹理缺失或无法解码时使用白色回退并记录警告。

PMX 支持 UTF-8 / UTF-16LE、多个材质分组、基础 UV 贴图和中文相对贴图路径。导入时请保留模型配套的贴图目录；模型路径栏、拖放及 `--model` 共用导入器。显示的是静态初始姿态；没有 VMD 动作、骨骼蒙皮、Morph、刚体/关节模拟、描边、Toon 或球面贴图着色。材质与贴图透明度按阈值 0.1 做裁切近似，零透明度材质隐藏，半透明混合尚未实现，界面会显示相应导入提示。已验证 PMX 2.0；PMX 2.1 的部分 Morph 和软体等扩展受当前 Assimp 实现限制。

本版本主要面向不透明材质与基本裁切材质；没有完整 PBR、混合透明、骨骼动画、变形动画、物理、脚本、场景保存或生产级大模型流式加载。模型资产仍由使用者提供并遵守其授权；仓库演示模型可直接离线使用。

glTF 的 OPAQUE 会忽略 alpha，MASK 使用模型指定的 cutoff（默认 0.5）；BLEND 暂按不透明显示并给出导入提示。glTF 外部缓冲和纹理的 URI 支持百分号编码、空格与 UTF-8 名称。

## 已验证的里程碑

2026-10-26 在本机 MSVC 19.51 / RTX 5070 / Vulkan SDK 1.4.357.0 上完成 Debug、Release 和 MinGW 编译。完整 Debug CTest **33/33 通过**，包含 SSE2／标量 Math 契约、公开接口、相机、模型导入、法线贴图、自动查找、MMD 效果绑定、字体、RHI 描述校验、模拟渲染命令、内嵌着色器及生成器十三项 CPU 测试，以及 **20 项真实 GPU 测试**。原有 **33 项模型/相机用例及 8 项字体用例**保留，法线导入与通用材质接口新增 13 组用例，自动查找新增 15 组用例，EMD/FX 绑定新增 14 组用例。Release 和 MinGW 的十三项 CPU 测试也通过验证。CPU 回归覆盖左右拖动时模型表面的投影运动方向，以及 PMX 编码、坐标/绕序、UV、材质、中文路径、透明裁切和错误输入；字体回归检查中文实际字形、Unicode 字体路径、字体数据所有权和无效字体诊断；GPU 测试另检查 PMX 场景确实可见。

GPU 测试检查场景截图确实包含模型，覆盖 OPAQUE/MASK 材质、含空格及中文资源 URI、窗口与视口反复缩放、导入替换失败后画面保持不变，以及 ImGui 初始化失败时的资源回收。测试同时检查 Vulkan 同步验证及退出阶段的错误输出。测试使用隐藏窗口；`-GpuTests` 仍需要可用的 Windows 桌面和 Vulkan 驱动。

Math 的普通 SSE2 和强制标量实现分别通过 16,724 个检查，覆盖布局、手算矩阵/四元数结果、归一化、失败回滚与别名、非有限值、极小/极大数量级，以及四种裁剪约定下的投影和视锥体。另用 10,000 组线性相关矩阵独立检查两种实现的奇异拒绝及输出保持。真实 GPU 回归检查四种约定下的方向、遮挡、投影切换与纹理运动；临时将运动矢量置零时会检出重投影滞后，恢复原着色器后通过。真实编辑器另验证了 PMX、自动法线贴图、相机拖拽及 TAA+FXAA，验证错误为零。

内嵌着色器测试逐字节比较七份静态数据与构建生成的 SPIR-V，检查大小、对齐、魔数、静态存储及无效枚举；生成器测试检查错误输入诊断和原输出保留。独立 Ninja 构建验证完整并行生成、GLSL／包含文件／提取脚本修改后的增量依赖和重复空构建。隔离部署测试只复制可执行文件和运行库，不携带 `shaders/` 或 SPIR-V 文件，分别执行 MSAA 4x＋FXAA 和 TAA＋FXAA，并检查非空场景截图及验证错误为零。Debug、Release、MinGW 均通过该部署验证。

抗锯齿 GPU 回归检查斜边的实际像素覆盖、纯色和模型内部颜色、MSAA/FXAA 叠加后的最终截图、恢复关闭后的像素一致性，以及单像素/奇数尺寸、窗口缩放、重复切换、错误参数和模型替换。当前 RTX 5070 支持全部四档；缺少部分采样档位的物理设备尚未实测。MinGW 命令行已另验证所有档位叠加 FXAA 的真实渲染，Vulkan 验证错误均为零。

TAA GPU 回归验证静态多帧累积与帧间波动、正交及透视相机/模型运动的纹理重投影、重置确定性、遮挡变化、最终 FXAA 截图及单像素视口。测试中临时将运动矢量置零会使纹理滞后断言失败，恢复正确着色器后通过。Release 和 MinGW 均另完成 32 帧 TAA 渲染与截图检查，验证错误为零。真实窗口检查了 TAA/FXAA 开关、历史计数、重置、旋转和聚焦；运动观感使用间隔截图检查，未做逐帧视频分析。

法线贴图回归覆盖 glTF / GLB / OBJ 导入、图片原始字节和材质强度、缺失纹理与不支持的坐标/变换回退、镜像 UV 分裂、退化 UV 隔离，以及非均匀/镜像节点变换。GPU 回归根据解析光照值检查线性色彩、X/Y 方向、空间变化的 UV 采样、全局/材质强度、双材质隔离和无效切线回退，也检查真实 glTF / GLB 导入后渲染的一致性、全部抗锯齿组合与 TAA 历史重置。Release 和 MinGW 另完成带法线贴图的 32 帧 TAA+FXAA 截图，验证错误均为零。

通用材质回归覆盖 PMX UTF-8 / UTF-16、OBJ、glTF / GLB 和无源文件程序模型的外部/内存法线图绑定、替换与清除、材质隔离、重复处理时网格数量稳定、中文与字面 `%20` 图片路径、UV 回退、无效图片/强度/几何的完整回滚。真实 GPU 测试验证这些格式的光照变化和清除后的像素恢复，另用可见性遮罩检查 PMX 未修改材质保持不变，以及 PMX 材质替换后的 TAA+FXAA 历史重置。

自动查找回归验证中文追加 `N`、各种后缀/前缀、颜色名称裁剪、目录范围、匹配层次、歧义、坏图、原生与手动优先级、完整回滚，以及实际 PMX / OBJ / glTF / GLB 导入。外部基础图片缺失或损坏时仍能保留原路径并发现配套法线。真实 GPU 测试中 PMX 首次渲染前没有调用手动绑定接口，自动加载后像素确实变化；清除后与原顶点法线画面一致，手动覆盖及 TAA+FXAA 历史重置也正常。

EMD/FX 回归验证默认项、材质覆盖、`none`、字符串宏与条件、包含路径、Unicode BOM 与旧编码注释/路径、错误索引、坏文件、循环及大小/数量限制、UV 平铺降级、续行和宏优先级。GPU 使用图片名称与模型/材质完全无关的原创资源，证明加载来源确实是 EMD→FX；检查其他材质像素保持不变，清除与手动覆盖以及 TAA+FXAA 重置正常。真实模型只读检查也确认了原材质索引与 EMD 绑定的对应关系。

真实窗口另检查了中文法线控件、材质计数，以及开关和强度 0/1 对凹凸细节的影响；TAA+FXAA 同时开启时行为正常，界面验证错误为零。验证使用项目原创的程序生成法线贴图和平面模型。

PMX 真实窗口检查了按材质应用、清除和重新应用外部法线图片；其他材质保持原样。无效路径会显示错误并保留原效果，TAA+FXAA 同时开启时验证错误为零。验证仅使用项目原创 PMX 小模型。

自动加载另在真实 PMX 窗口中验证：不点击“应用”即可加载基础图片旁的 `N` 后缀法线图；手动清除后保持关闭，手动覆盖正常，重新导入恢复自动查找。验证错误为零，使用的仍是项目原创模型和生成图片。

EMD 真实窗口验证确认了声明图片自动加载、手动清除/覆盖以及重新导入恢复绑定。实际 MMD 模型的 32 个材质中仅两项加载了 EMD 指定的法线图，材质索引与文件声明对应，界面验证错误为零；验证过程中没有修改模型或配套文件。

## Math 数学约定

数学库公开接口为 `engine/math/public/math.hpp`，命名空间 `aeg::math`，链接目标 `aeg::math`。实现位于同模块 `private/`，无第三方数学库依赖；SSE2 与标量回退只影响内部矩阵乘法实现，不改变公开类型或 ABI。

- 世界和视图均为右手坐标系：+X 向右、+Y 向上、相机沿 -Z 观察；角度为弧度，正旋转遵循右手定则。编辑器旋转控件保留角度显示，在调用 Math 时显式转换。
- `Vec2/Vec3/Vec4` 的分量分别为 xy/xyz/xyzw；`Quat` 按 xyzw 存储，默认 `(0,0,0,1)`。旋转接口要求单位四元数，不会偷偷归一化输入。
- `Vec3` 为 16 字节大小、16 字节对齐，尾部四字节是非语义填充，不能按连续三个浮点数序列化。`Vec2` 为 8 字节；`Vec4`、`Quat` 为 16 字节；`Mat4` 为 64 字节、16 字节对齐。顶点布局为 position0、normal16、uv32、tangent48，步长64；上传使用完整顶点结构及显式偏移。
- `Mat4{}` 是零矩阵，单位矩阵必须使用 `identity()`。矩阵由四个列向量组成，`operator[]` 按列访问，`at(row,column)` 按行列访问。列向量在右侧相乘：`clip = projection * view * model * local`；`trs(t,r,s)` 等价于 `translation(t) * from_quat(r) * scale(s)`。Shader 使用相同列主序，上传不做隐式转置。
- 点变换使用 w=1 并做透视除法；向量变换使用 w=0，不受平移影响。`try_*` 的无效输入、非有限结果及数值奇异变换会返回 false，并完整保留原输出，包括输入和输出别名的情况。
- 值返回包装函数失败时返回零向量或零矩阵；四元数的 normalize、inverse 和轴角构造失败时返回单位四元数。`operator==` 逐分量精确比较；有限数的近似比较使用显式绝对容差的 `nearly_equal()`。`kEpsilon` 是数值阈值，不是世界尺度或时间步长。
- `perspective()`、`orthographic()` 默认采用右手 0..1 深度；near/far 为正距离，可见相机 Z 在 `[-far,-near]`。`ClipSpaceConvention{y_down,reversed_z}` 分别控制 NDC Y 方向及 near1/far0 的反向深度；默认 near0/far1。公共 API 不提供 OpenGL -1..1 深度语义。
- `RenderCamera::view_projection()` 返回 projection*view，`RenderSettings::set_camera()` 同时传递矩阵和对应的裁剪约定。渲染器显式转换到 Vulkan 正高度视口的 Y 方向；反向深度使用 clear0/Greater，普通深度使用 clear1/Less。TAA 对应转换当前帧和前一帧，切换约定时清空历史。
- 平面方程为 `dot(normal,point)+offset`，内部取值 >=0；视锥面顺序固定为 Left、Right、Bottom、Top、Near、Far，对应 `FrustumPlane::kLeft` 至 `kFar`。`try_extract_frustum()` 使用对应裁剪约定并生成归一化平面；失败时保留输出。点和球体测试要求调用方先确保视锥体有效、平面已归一化；其他模块复用 Math 的提取函数。

矩阵求逆使用按原始行幅度缩放的主元选择，主元比例及倒条件估计 `1/(||A||∞*||A⁻¹||∞)` 均须大于所给相对阈值，结果须有限。仿射矩阵单独检查 3×3 线性部分，排除平移量；整体很小但条件良好的缩放和大平移仍可求逆，默认阈值会拒绝极端各向异性或近线性相关的矩阵。

秩检查还比较双精度行列式与其各项绝对值之和，按双精度机器精度识别抵消导致的数值奇异，不以世界单位设定下限。投影构造、点/向量变换及视锥提取只要求双精度可逆，不要求逆矩阵能够写入浮点 `Mat4`；极小近裁面和统一齐次缩放不会因此误拒绝。SSE2 和标量路径共享契约用例，允许轻微舍入差异，失败规则保持一致。

只验证 Math 时无须 Vulkan SDK 或其他依赖（需要 C++23 编译器与 Ninja）：

```powershell
cmake -S engine/math -B build/math-standalone -G Ninja
cmake --build build/math-standalone
ctest --test-dir build/math-standalone --output-on-failure
```

`BUILD_TESTING=ON` 时生成普通实现及强制标量实现两个测试目标。`aeg_core_tests` 聚合目标也会构建这些 Math 测试。

## 代码结构与依赖

| 路径 | CMake 目标 | 命名空间 / 职责 |
| --- | --- | --- |
| `engine/math/` | `aeg::math` | `aeg::math`，自研向量、四元数、矩阵、投影与视锥体 |
| `engine/foundation/` | `aeg::foundation` | `aeg::foundation`，Bounds、UTF-8 路径处理 |
| `engine/camera/` | `aeg::camera` | `aeg::camera`，轨道相机与携带裁剪空间约定的 RenderCamera |
| `engine/assets/` | `aeg::assets` | `aeg::assets`，CPU 模型数据、Assimp 导入、纹理解码、PMX 校验 |
| `engine/rhi/` | `aeg::rhi` | `aeg::rhi`，设备、资源、绑定、管线、命令、交换链的通用接口与描述校验 |
| `engine/rhi/vulkan/` | `aeg::rhi_vulkan` | `aeg::rhi::vulkan`，Vulkan 设备、内存、同步、交换链及着色器编译适配 |
| `engine/gui/` | `aeg::gui` | `aeg::gui`，ImGui 绘制及纹理注册适配接口 |
| `engine/rhi/vulkan/private/imgui/` | `aeg::rhi_vulkan_gui` | Vulkan ImGui 适配实现，可访问后端内部原生句柄 |
| `engine/render/` | `aeg::render` | `aeg::render`，通过 RHI 实现场景、材质、视口、抗锯齿、截图 |
| `editor/` | `aeg::editor` | `aeg::editor`，窗口、帧协调、GUI 纹理生命周期、停靠、面板与字体 |
| `apps/editor/private/` | `aeg_editor` | 全局 main、`aeg::app` 命令行入口与 Vulkan 工厂装配 |
| `tests/{math,foundation,camera,assets,rhi,render,editor}/` | 对应测试目标 | 与功能目录对应的 CPU/GPU 回归；数据保留在 `tests/fixtures/` |
| `assets/` | 运行时复制 | 离线演示模型与纹理 |

功能性 CMake 模块统一放在根目录 `cmake/`：`ProjectOptions.cmake` 管理编译器检测及目标编译／链接选项，`MathLibrary.cmake` 定义普通／标量数学库目标，`VulkanShaders.cmake` 定义着色器构建规则，`EmbedSpirv.cmake` 提取 SPIR-V 头文件；依赖修补和测试驱动脚本也集中在该目录。根入口加载这些模块，各功能目录的 `CMakeLists.txt` 声明目标、依赖和测试。

各模块都有自己的 `CMakeLists.txt`。公开头文件直接位于 `public/`，内部头文件与源文件位于 `private/`。例如：

```text
engine/render/
├─ CMakeLists.txt
├─ public/
│  ├─ Renderer.hpp
│  ├─ RenderSettings.hpp
│  └─ AntialiasingSettings.hpp
└─ private/
   ├─ Renderer.cpp
   ├─ RendererImpl.hpp
   ├─ scene/
   ├─ viewport/
   ├─ antialiasing/
   └─ shaders/{scene,postprocess}/
```

RHI 的十个公开头文件平铺在 `engine/rhi/public/`，只依赖 C++ 标准库，不包含 Vulkan、GLFW、ImGui、Windows 或模型数据类型。Vulkan 资源、命令与呈现实现分别放在 `engine/rhi/vulkan/private/{device,resources,bindings,pipelines,commands,presentation}/`。模型和材质上传、场景绘制、视口、MSAA／TAA／FXAA 与截图转换均通过真实 RHI 对象和命令实现，通用 Renderer 不调用 Vulkan，也不持有 ImGui 或窗口对象。

ImGui 分成 core、GLFW 平台和 Vulkan 绘制三个目标。应用入口在创建窗口后通过工厂回调装配 Vulkan 设备、交换链和 GUI 适配器；`EditorGraphics` 协调等待与获取图像、UI 构建、场景与后处理、UI 绘制、提交及呈现。只有成功提交才推进 TAA 历史，呈现要求重建不会回滚已提交帧。GUI 使用稳定的纹理注册标识，并保留输出图像所有权直到注册更新或移除。

GLSL 保留在渲染模块，Vulkan 专用编译规则和 SPIR-V 头文件提取分别位于 `cmake/VulkanShaders.cmake`、`cmake/EmbedSpirv.cmake`。`ShaderLibrary` 通过枚举返回七份内嵌数据的只读 `span`，普通场景、TAA 场景、全屏、FXAA 与 TAA 管线都使用这一入口；`Renderer` 只接收 RHI 设备，不再接收着色器目录。CMake 为渲染库本身声明生成依赖，直接构建 `aeg_render` 也会先生成所需头文件。修改 GLSL、包含文件或提取脚本后会自动更新生成结果，内容相同时保留头文件时间戳，避免无效重编译。生成文件保留在构建目录，不加入源码仓库。

使用模块时链接其 CMake 目标，通过扁平头文件名引用公开接口：

```cmake
target_link_libraries(my_tool PRIVATE aeg::camera aeg::render)
```

```cpp
#include <OrbitCamera.hpp>
#include <ModelImporter.hpp>
#include <Renderer.hpp>

aeg::camera::OrbitCamera camera;
auto model = aeg::assets::loadModel(path);
```

CMake 仅导出各模块的 `public/`；Vulkan GUI 桥接与核心后端属于同一后端模块，内部共用私有互操作声明。RHI 的十个头文件和 Math 头文件接受独立编译检查，公开接口测试不获得模块的 private 搜索路径。字体白盒测试单独获得 `editor/private/fonts/`，该目录不会传递给应用程序。GPU 测试复用产品的 `EditorGraphics`、Renderer 和 Vulkan 后端；测试辅助类只提供调用转发，不重编另一份渲染实现。`aeg_core_tests` 保留为 Math、相机、模型导入与法线贴图导入测试的构建聚合目标。

可单独验证不需要图形 SDK 的 RHI 接口（需要可用 C++23 编译器与 Ninja）：

```powershell
cmake -S tests/rhi/standalone -B build/rhi-standalone -G Ninja
cmake --build build/rhi-standalone
ctest --test-dir build/rhi-standalone --output-on-failure
```

模拟 RHI 命令测试只链接通用 Renderer，检查场景→TAA→FXAA 顺序、材质颜色空间、未提交帧不推进历史、重复提交通知、替换失败回退、历史重置保留最后画面及缩放后拒绝过期截图。真实 GPU 回归继续保留法线贴图、AA、重投影与生命周期的像素和错误路径断言。

固定依赖版本及 SHA256 校验在 `cmake/Dependencies.cmake`：

| 依赖 | 固定版本 | 官方来源 |
| --- | --- | --- |
| GLFW | 3.4 | [glfw/glfw](https://github.com/glfw/glfw/releases/tag/3.4) |
| Assimp | v5.4.3，OBJ / glTF / MMD(PMX) 导入器，静态库、内置 zlib | [assimp/assimp](https://github.com/assimp/assimp/releases/tag/v5.4.3) |
| Dear ImGui | v1.91.9b-docking，GLFW / Vulkan 后端 | [ocornut/imgui](https://github.com/ocornut/imgui/releases/tag/v1.91.9b-docking) |
| stb_image | commit `003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5` | [nothings/stb](https://github.com/nothings/stb/commit/003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5) |

依赖源码缓存于 `build/_deps/*-src`，Debug 与 Release 使用独立编译目录。第三方许可证保留在各依赖源码目录中；发布二进制时需同时提供相应许可证。

项目通过 `cmake/ApplyAssimpMmdPatch.cmake` 自动应用针对固定版本的 MMD 修正：附加 UV 索引以及场景转换前的网格、材质、贴图和骨骼引用检查。补丁有明确源码锚点并可重复配置；更换 Assimp 版本时需要一并检查补丁。回归 PMX 均由 `tests/fixtures/generate_pmx.py` 生成，为项目原创小模型。

`cmake/ApplyImGuiVulkanPatch.cmake` 为固定版本 ImGui 的字体上传暂存资源增加异常清理。字体暂存内存分配、映射或刷新失败时，会释放已创建的缓冲和内存，再由适配器清理其余初始化状态。补丁校验完整原始／修改后源码哈希，可重复配置；升级 ImGui 时需重新核对。实际 Vulkan 错误注入覆盖描述符布局创建及上述三个字体上传失败位置，均验证退出清理无存活资源。

若出现 Vulkan 初始化错误，先检查显卡驱动与 SDK；验证层诊断输出在终端。若编译器或 SDK 路径改变，可删除对应的 `build/debug` 或 `build/release` 目录后重新配置，保留共享源码缓存即可。
