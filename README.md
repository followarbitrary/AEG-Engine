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
- **方向 Gizmo**：视口右上角显示世界方向轴，拖动可环绕相机，点击轴端可切换前、后、左、右、顶、底视角；重复点击当前正对的轴端可翻转到另一侧。切换保持观察中心和距离，顶视、底视仍可平移与环绕。
- **变换 Gizmo**：当前模型默认为选中对象，在视口左上角或用 `W / E / R` 切换移动、旋转、缩放。拖动轴线、平面或旋转环可编辑整体变换，手柄以模型包围盒中心为轴心，属性面板同步更新。移动和旋转可切换局部／世界坐标，缩放使用局部坐标并保留负缩放符号；`Esc` 撤销本次拖动。拖动期间暂停自动旋转，释放后恢复；文本编辑期间快捷键暂停。当前不单独选择或变换子网格。
- **PBR 光照**：在“材质”中设置所选材质的金属度和粗糙度，点击“应用 PBR 参数”；在“光照”中调整方向光、点光源、聚光灯及环境漫反射。普通相机环绕会改变高光位置，无光照模式仍直接显示基础颜色。
- **法线贴图**：导入模型时自动读取材质声明的法线图，缺少声明时按文件名查找配套图片；所有已支持格式共用此规则。仍可在属性面板“材质”中选择材质，输入 RGB 法线图片路径，应用、替换或清除贴图；相对路径以模型所在目录为基准，也支持绝对路径和中文路径。“光照”中的“使用法线贴图”和 0–4 的“法线强度”控制全部材质，默认启用、强度为 1；强度为 0 时使用原来的顶点法线。无光照模式不受法线贴图影响，界面同时显示带法线贴图的材质数。
- **透明材质**：导入时自动读取透明模式和透明度；在属性面板“材质”中选择材质后，可切换“不透明／透明裁切／半透明”、调整不透明度及裁切阈值，再点击“应用透明设置”。修改只作用于所选材质，上传失败时保留原模型；修改成功后重置 TAA 历史。
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

## 透明材质渲染

材质通过 `aeg::assets::AlphaMode` 明确指定三种模式，基础颜色因子与基础贴图的 Alpha 相乘一次，作为有效透明度：

| 模式 | 行为 |
| --- | --- |
| `Opaque` | 忽略 Alpha，写入颜色和深度 |
| `Mask` | 有效 Alpha 小于 cutoff 时丢弃，其余像素完全不透明并写入深度 |
| `Blend` | 使用有效 Alpha 做线性颜色混合；进行深度测试，关闭深度写入；零 Alpha 不产生颜色或时域信息 |

glTF／GLB 严格遵守 `OPAQUE`、`MASK`、`BLEND`，默认模式为 `OPAQUE`，MASK 默认 cutoff 为 0.5。OBJ／MTL 的 `d`、`Tr` 和 PMX 的材质透明度会被保留；这些格式在因子小于 1 或贴图含部分透明像素时使用 Blend，仅有二值透明像素时使用 Mask，否则使用 Opaque。PMX 的 Mask cutoff 保留为 0.1。程序模型默认 Opaque，调用方显式选择模式。

OBJ 的 `map_d` 使用图片的灰度 R 通道，按线性数值合并到基础图片 Alpha。不同尺寸按归一化 UV 的像素中心做最近邻重采样，保留基础 RGB；合并限制为单边 16384、总计 6400 万像素。需要 UV0；不支持的坐标、变换或纹理选项会提示并保留基础图片，缺失或损坏的透明贴图也会诊断回退。

渲染器先画不透明与裁切网格，再按网格包围盒中心的相机深度从远到近稳定排序半透明网格，支持透视、正交、Y 方向及反向 Z 约定。半透明使用 `src.rgb * alpha + dst.rgb * (1-alpha)`，复用基础颜色、贴图与法线光照。排序以网格为单位，交叉网格和单个网格内部相互重叠的三角形仍可能出现顺序误差；当前没有顺序无关透明或折射。

MSAA 和 FXAA 可处理透明边缘。TAA 对当前半透明像素及其相邻像素绕过历史累积，并写入无效历史深度，避免多层透明污染不透明物体的重投影；这些区域使用当前帧结果，TAA 模式下可叠加 FXAA。设备需要独立附件混合能力才能启用 TAA，其他 AA 模式仍可使用。材质修改继续沿用候选模型验证、GPU 上传成功后替换的流程。

公开修改接口位于 `engine/assets/public/MaterialProperties.hpp`：

```cpp
aeg::assets::setMaterialAlpha(model, materialIndex, aeg::assets::AlphaMode::Blend, 0.5f);
renderer.uploadModel(model);
```

模式必须有效，不透明度必须有限且位于 0..1，cutoff 必须有限且不小于 0。设置失败时保持模型不变；设置成功时保留几何、图片、法线设置及其他材质。编辑器的材质修改只在当前会话生效。

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

## PBR 直接光照

场景使用 Cook–Torrance 微表面模型：GGX 法线分布、Smith GGX 高度相关几何遮蔽和 Schlick Fresnel。介质的法向反射率为线性 0.04，金属的反射颜色来自基础颜色；漫反射按 Fresnel 和金属度减少。基础颜色纹理继续以 sRGB 读取并在线性空间参与光照；法线贴图、透明裁切和 Alpha 混合复用相同的 BRDF。

所有支持的模型格式共用 `MaterialData::metallicFactor` 和 `roughnessFactor`，取值范围为 `[0,1]`。PMX、普通 OBJ 和程序创建的材质默认非金属、粗糙度 0.5；glTF/GLB 读取原生因子，包括原生缺省值 1。粗糙度 0 可以导入、编辑和保存于 CPU 数据中，着色计算使用最低 0.045 避免奇异高光。本阶段只支持标量因子，模型声明金属度/粗糙度贴图时会显示导入警告。

```cpp
aeg::assets::setMaterialPbr(model, materialIndex, 0.8f, 0.25f);
renderer.uploadModel(model);
settings.set_camera(camera.render_camera(aspect));
settings.lights[0].color = {1.0f, 0.9f, 0.8f}; // 线性 RGB
settings.lights[0].intensity = 3.0f; // 相对光照强度，不是物理光度单位
```

`setMaterialPbr` 对索引、有限值和范围进行完整验证，失败保持整个模型不变；编辑器在 GPU 上传成功后才替换 CPU 模型。`RenderSettings::set_camera()` 同时传递矩阵、裁剪约定和世界空间视线，正交相机使用固定视线。直接赋值 `viewProjection` 的调用方也必须同步 `cameraPosition` 和 `cameraTowardViewer`。光源颜色、强度和材质改变会重置 TAA 历史；普通相机移动继续重投影。

环境漫反射是 `ambient * baseColor * (1-metallic)` 的近似补光，只添加一次。本阶段没有 IBL 或环境反射，纯金属未受直接光照时会较暗。场景仍使用现有线性 LDR 输出，高强度高光会饱和；HDR、曝光及色调映射尚未加入。

公式参考 [Filament 材质模型](https://google.github.io/filament/main/filament.html)；材质因子约定参考 [glTF 2.0 规范](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#metallic-roughness-material)。

## 多光源、CSM 与 PCF 阴影

在“光照”面板添加、选择、删除方向光、点光源和聚光灯，最多八盏；可独立控制开关、颜色、强度、位置、方向和范围。聚光灯的内外锥角在界面使用角度，公开 API 使用弧度半角。新点光源和聚光灯按当前模型尺寸初始化位置、范围和强度。光源由编辑器或 `RenderSettings` 创建，本轮不导入模型文件中的场景灯光节点。

`RenderSettings::lights` 是固定八元素数组，`lightCount` 默认 1，默认首盏为方向光；计数为 0 时仅保留环境漫反射。点光源与聚光灯按反平方距离衰减，并在指定范围附近平滑归零：`max(1-(distance/range)^4,0)^2 / max(distance,0.01)^2`。距离下限 0.01 是引擎的近光源正则化尺度；与光源位置完全重合时不计算方向未定义的直接光。聚光灯在内外锥角的余弦之间使用平滑过渡，相等角度形成硬边。方向光忽略位置和范围。

```cpp
settings.lightCount = 2;
auto& point = settings.lights[1];
point.type = aeg::render::LightType::Point;
point.position = {2, 3, 2};
point.color = {0.7f, 0.85f, 1.0f};
point.intensity = 50;
point.range = 12;

aeg::render::ShadowSettings shadow;
shadow.enabled = true;
shadow.lightIndex = 0; // 默认方向光
shadow.resolution = 1024;
shadow.cascadeCount = 4; // 方向光默认 4 级；聚光灯仍只渲染第 1 张
shadow.splitLambda = 0.5f;
shadow.maxDistance = 0; // 自动使用相机远裁面，也可以设置正的视空间距离上限
shadow.blendFraction = 0.1f;
shadow.distanceFadeFraction = 0.1f;
shadow.pcfRadius = 1; // 0 硬阴影，1 为 3x3，2 为 5x5
renderer.setShadows(shadow);
```

“阴影”面板默认关闭，开启后选择一盏方向光或聚光灯。方向光默认使用 4 级 CSM，可切换 1–4 级；1 级保留覆盖整个模型的原有单张投影，2–4 级按相机深度划分视锥。分割比例从均匀分割（0）过渡到对数分割（1），数值越大越侧重近处精度。自动距离使用相机远裁面，手动距离以相机视空间正距离为上限。级联数量、每级分辨率和 PCF 范围共同影响质量与开销。

每一级根据未抖动的相机视锥分段拟合稳定球体，按相对尺度量化投影大小并进行阴影纹素对齐，减少相机轻微移动时的闪烁。XY 范围覆盖接收区域，深度范围保留整个模型的遮挡物，避免相机视野之外的物体丢失投影。相邻级联有重叠过渡区，并在最后一级距离上限前淡出；过渡和淡出比例均可调整。阴影独立于视口的 Y 方向、正反向 Z 和 TAA 抖动。

过滤可选硬阴影、3×3 PCF、5×5 PCF。每个采样使用接收面深度梯度修正实际纹素中心的比较，减少斜面自遮挡；所有活动级联的导数在材质裁切和逐像素级联选择前计算，防止接缝处导数失效。视锥外的接收点或滤波采样视为未遮挡。聚光灯始终沿用单张透视阴影，同样支持三档过滤。CSM 的稳定拟合、重叠与过滤参考 [Microsoft CSM 指南](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps)，引擎保留自己的右手、列主序和 0–1 深度约定。

阴影只影响所选光源的直接光，保留其他光源和环境补光。不透明与裁切材质投影，裁切继续使用基础纹理 Alpha、材质 Alpha 和相同阈值；半透明材质接收阴影，本轮不投影。点光源的六面全向阴影、透明投影和场景地面网格尚未加入；当前模型可自投影，包含地面/接收面的模型也可呈现投射阴影。

每级分辨率为 16–4096 的二次幂，同时受设备能力约束。引擎分别查询可采样深度格式及实际尺寸，优先 D32、其次 D16、D24；不支持时禁用阴影，保留其他光照。四张独立深度图共用 Pass 和管线，所有级联矩阵在一帧中只上传一次，逐次绘制通过顶点阶段常量选择级联。未使用的纹理绑定为已初始化的白色回退，聚光灯首先启用或与方向光切换时也不会引用未渲染的深度图。`setShadows()` 在完整候选资源创建成功后替换，失败保留旧设置、画面和 TAA 历史；分辨率和级联数增加需要更多显存，分配失败不会静默降低设置。光源活动属性、数量、材质或阴影设置改变会重置 TAA；普通相机和模型移动继续重投影。

公开参数要求有限值：分割比例 `[0,1]`、过渡比例 `[0,0.3]`、远端淡出比例 `[0,0.5]`、距离上限非负、PCF 半径 0–2。无效设置在修改资源前被拒绝。某帧相机/模型变换无法拟合时，该帧禁用阴影并使用全白回退，不保留旧级联数据；有效输入恢复后自动继续绘制。CSM 调用方必须传入匹配的相机矩阵、世界空间位置与视线，推荐使用 `RenderSettings::set_camera()`。

原始无效光源输入会确定性处理：计数最多取八，非法类型/位置/范围不参与照明，零方向回退向下，非有限或负的颜色/强度视为零；方向光不检查未使用的位置。点光源强度在距离衰减后才限制入射亮度，有限远距离按顺序除法避免平方溢出。光照衰减参考 [Khronos 点光源规范](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_lights_punctual) 的模型，本引擎使用相对强度与平方平滑截止。

## 范围与限制

当前里程碑支持静态 OBJ / glTF / GLB / PMX 网格、节点变换、基础颜色、常见基础颜色纹理、RGB 法线贴图、深度测试、PBR 多光源、方向光 CSM 与聚光灯阴影。基础颜色纹理缺失或无法解码时使用白色回退并记录警告。

PMX 支持 UTF-8 / UTF-16LE、多个材质分组、基础 UV 贴图和中文相对贴图路径。导入时请保留模型配套的贴图目录；模型路径栏、拖放及 `--model` 共用导入器。显示的是静态初始姿态；没有 VMD 动作、骨骼蒙皮、Morph、刚体/关节模拟、描边、Toon 或球面贴图着色。材质与贴图支持不透明、裁切和半透明模式，零透明度的半透明材质隐藏。已验证 PMX 2.0；PMX 2.1 的部分 Morph 和软体等扩展受当前 Assimp 实现限制。

本版本支持 PBR 直接光照与排序 Alpha 混合；没有骨骼动画、变形动画、物理、脚本、场景保存或生产级大模型流式加载。模型资产仍由使用者提供并遵守其授权；仓库演示模型可直接离线使用。

glTF 的 OPAQUE 会忽略 alpha，MASK 使用模型指定的 cutoff（默认 0.5），BLEND 使用实际 Alpha 混合。glTF 外部缓冲和纹理的 URI 支持百分号编码、空格与 UTF-8 名称。

## 已验证的里程碑

本轮多光源增加 31 次解析 GPU 截图，阴影增加 41 次解析 GPU 截图，覆盖反平方距离、近光源下限、范围截止、聚光锥角、彩色光源叠加与一次环境补光，以及遮挡、未遮挡接收面、选定光源索引、裁切孔洞、半透明材质、PCF 边缘、模型变换和全部 AA/裁剪约定。CPU 回归覆盖可采样深度能力及格式回退、尺寸限制、描述符与共享常量、Pass 顺序，以及纹理、管线、描述符、等待失败时的完整回滚。极远有限点光源溢出、固定 NaN 导致 TAA 重置、低能力设备默认分辨率及倾斜面 PCF 自遮挡均有先失败后通过的回归；临时禁用阴影查询会使遮挡探针从预期 61 变为 112，恢复原着色器后通过。真实窗口验证了方向光、点光源/聚光灯编辑、投影光源与分辨率切换、TAA+FXAA，Vulkan 验证错误为零。

CSM 新增 42 次解析 GPU 截图，验证四个级联实际参与遮挡、视空间深度选择、PCF 三档滤波、级联混合、距离淡出和视野外遮挡物，覆盖全部 AA／裁剪约定。混合测试检查超出单张 3×3 PCF 离散可见度的连续亮度，避免只因投影拟合变化而误判通过；临时关闭级联混合、复用首张阴影图或关闭 PCF 均会触发对应失败。CPU 回归还覆盖稳定拟合、重叠区域、有限阴影距离下的大远近裁面比例、无效帧白色回退与恢复，以及部分资源分配失败回滚。真实窗口验证了 2/3/4 级切换、5×5 PCF、自动／手动距离、过渡与淡出编辑，以及相机运动时的 TAA+FXAA，运行 13,223 帧并正常退出，Vulkan 验证错误为零。

Gizmo 回归通过真实 ImGui 输入事件覆盖移动、旋转、缩放、模型中心轴心、反射变换的拖动方向、拖出窗口后的持续操作与释放、Esc 撤销、快捷键抑制和非等比帧缓冲缩放。独立矩阵测试验证现有 `Rx*Ry*Rz` 旋转顺序、负／非均匀缩放、欧拉角连续性、奇异与非有限输入回退；相机测试覆盖六向切换、精确顶／底视、平移与原有缩放范围。初轮真实窗口验证了三种手柄及顶视切换，运行 21,609 帧并正常退出，Vulkan 验证错误为零；后续边界修正由上述输入事件回归验证。

2026-10-26 在本机 MSVC 19.51 / RTX 5070 / Vulkan SDK 1.4.357.0 上完成 Debug、Release 和 MinGW 编译。完整 Debug CTest **46/46 通过**，包含 21 项 CPU 测试与 25 项真实 GPU 测试；Release 和 MinGW 的 21 项 CPU 测试均通过。回归覆盖 SSE2／标量 Math、公开接口、相机与缩放范围、模型导入、法线贴图、自动查找、MMD 效果绑定、透明材质、PBR 材质参数、字体、Gizmo、RHI、渲染命令及内嵌着色器。

PBR 新增四组材质 CPU 契约和 73 次解析 GPU 截图，使用独立双精度 Smith Lambda 参考及法向入射闭式解核对 BRDF；覆盖金属度、粗糙度零值、掠射角、光源颜色/强度、世界空间视线、正交/透视、镜像与非均匀变换、全部 AA/裁剪约定，以及 TAA 历史重置。分别改错 GGX 粗糙度、关闭 Fresnel 和跳过 Smith，测试均能检出，恢复后通过。Release 和 MinGW 另以实际 PMX 模型完成 32 帧 TAA+FXAA 渲染、截图可见性检查，Vulkan 验证错误均为零；真实窗口验证材质参数应用和光源强度即时更新。半透明 glTF 的可见性冒烟与 PMX 一样采用受控白色背景和无光照条件，保留原有对比度、覆盖率和颜色数量门槛；光照与混合的正确性由解析回归单独验证。

GPU 测试检查场景截图确实包含模型，覆盖 OPAQUE/MASK 材质、含空格及中文资源 URI、窗口与视口反复缩放、导入替换失败后画面保持不变，以及 ImGui 初始化失败时的资源回收。测试同时检查 Vulkan 同步验证及退出阶段的错误输出。测试使用隐藏窗口；`-GpuTests` 仍需要可用的 Windows 桌面和 Vulkan 驱动。

Math 的普通 SSE2 和强制标量实现分别通过 16,724 个检查，覆盖布局、手算矩阵/四元数结果、归一化、失败回滚与别名、非有限值、极小/极大数量级，以及四种裁剪约定下的投影和视锥体。另用 10,000 组线性相关矩阵独立检查两种实现的奇异拒绝及输出保持。真实 GPU 回归检查四种约定下的方向、遮挡、投影切换与纹理运动；临时将运动矢量置零时会检出重投影滞后，恢复原着色器后通过。真实编辑器另验证了 PMX、自动法线贴图、相机拖拽及 TAA+FXAA，验证错误为零。

透明材质 CPU 回归覆盖 glTF／GLB 模式、OBJ 的 d／Tr／RGBA／map_d、PMX 编码与签名识别、参数校验、完整回滚及法线属性保持。GPU 使用 139 张截图检查解析混合颜色、多层与输入顺序、遮挡、Alpha 0／1、贴图与因子相乘、裁切、法线光照、各投影/裁剪约定、全部可用 AA 组合及 TAA 历史保护。强制输出 Alpha 为 1 会检出混色错误；禁用透明响应标记会使重投影误差从 0.0465 升至 4.5116，超过阈值 0.35，恢复原着色器后通过。实际 PMX 透明场景使用受控背景和光照，保持原可见性检查的对比度、覆盖率及颜色数量要求。真实窗口另验证了所选材质的半透明编辑及 TAA+FXAA，Vulkan 验证错误为零。

内嵌着色器测试逐字节比较九份静态数据与构建生成的 SPIR-V，检查大小、对齐、魔数、静态存储及无效枚举；生成器测试检查错误输入诊断和原输出保留。独立 Ninja 构建验证完整并行生成、GLSL／包含文件／提取脚本修改后的增量依赖和重复空构建。隔离部署测试只复制可执行文件和运行库，不携带 `shaders/` 或 SPIR-V 文件，分别执行 MSAA 4x＋FXAA 和 TAA＋FXAA，并检查非空场景截图及验证错误为零。Debug、Release、MinGW 均通过该部署验证。

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

GLSL 保留在渲染模块，Vulkan 专用编译规则和 SPIR-V 头文件提取分别位于 `cmake/VulkanShaders.cmake`、`cmake/EmbedSpirv.cmake`。`ShaderLibrary` 通过枚举返回九份内嵌数据的只读 `span`，普通场景、TAA 场景、阴影、全屏、FXAA 与 TAA 管线都使用这一入口；`Renderer` 只接收 RHI 设备，不再接收着色器目录。CMake 为渲染库本身声明生成依赖，直接构建 `aeg_render` 也会先生成所需头文件。修改 GLSL、包含文件或提取脚本后会自动更新生成结果，内容相同时保留头文件时间戳，避免无效重编译。生成文件保留在构建目录，不加入源码仓库。

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
| ImGuizmo | 1.10，commit `b796ac3b861afc6e91ca74e4611effd9c9527367`，仅变换手柄核心 | [CedricGuillemet/ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo/releases/tag/1.10) |
| stb_image | commit `003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5` | [nothings/stb](https://github.com/nothings/stb/commit/003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5) |

依赖源码缓存于 `build/_deps/*-src`，Debug 与 Release 使用独立编译目录。第三方许可证保留在各依赖源码目录中；发布二进制时需同时提供相应许可证。

项目通过 `cmake/ApplyAssimpMmdPatch.cmake` 自动应用针对固定版本的 MMD 修正：附加 UV 索引以及场景转换前的网格、材质、贴图和骨骼引用检查。补丁有明确源码锚点并可重复配置；更换 Assimp 版本时需要一并检查补丁。回归 PMX 均由 `tests/fixtures/generate_pmx.py` 生成，为项目原创小模型。

`cmake/ApplyImGuiVulkanPatch.cmake` 为固定版本 ImGui 的字体上传暂存资源增加异常清理。字体暂存内存分配、映射或刷新失败时，会释放已创建的缓冲和内存，再由适配器清理其余初始化状态。补丁校验完整原始／修改后源码哈希，可重复配置；升级 ImGui 时需重新核对。实际 Vulkan 错误注入覆盖描述符布局创建及上述三个字体上传失败位置，均验证退出清理无存活资源。

ImGuizmo 仅编译变换手柄核心，复用现有 ImGui，不引入其他数学库或渲染后端。`editor/private/gizmo/` 负责引擎矩阵、中心轴心、负缩放与输入适配，方向 Gizmo 使用引擎相机。`cmake/ApplyImGuizmoPatch.cmake` 修正固定版本中旋转／缩放手势离开所属窗口后跳过移动与释放处理的问题；补丁校验完整源码哈希并可重复配置，升级依赖时须重新检查。

若出现 Vulkan 初始化错误，先检查显卡驱动与 SDK；验证层诊断输出在终端。若编译器或 SDK 路径改变，可删除对应的 `build/debug` 或 `build/release` 目录后重新配置，保留共享源码缓存即可。
