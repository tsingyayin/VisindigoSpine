# Visindigo (Qt) Spine Widget 
Visindigo Spine 是 Visindigo::Widgets 下用于显示Spine内容的基于QtOpenGLWidget的套件。此 Visindigo SpineWidget 主要是提供在Qt框架（尤指QtWidgets技术栈中）中播放Spine动画的功能，不包括对现有Spine资源的改动、编辑以及其他操作功能。

从架构上而言，spine-cpp被完全隔离在动态链接库内部，使用此类不需要再另外引入任何spine-cpp的组件——也正因为此，SpineWidget真的只能提供基本的播放功能，所有功能全部都在spinewidget.h定义，不在此处列出的任何Spine功能均无法访问。

此库是Visindigo项目的一部分，由于许可证问题，此库不能随Visindigo( [Yayin Story Studio](https://github.com/tsingyayin/YayinStoryStudio) )一同使用LGPLv2.1发布，因此独立的在此提供。

由于未使用其他Visindigo项目的代码，因此此库可以独立的在任何Qt项目中使用，维需恪守 Spine Runtimes License Agreement 进行使用，这是Spine官方强制要求所有Spine Runtimes 必须遵守的许可证。

# 使用项目
1. 本项目未重新分发spine-cpp 4.2的源代码，使用本项目的用户必须自行从[Spine官方](https://esotericsoftware.com/spine-cpp-download)下载spine-cpp 4.2的源代码，并将其放置在本项目的`src/spine-cpp`目录下。这个目录已经有配置好的vcxproj文件，用户将spine\include和spine\src目录添加到项目中即可。
2. 运行slnx，以VISpineWidgetDemo为启动项目进行构建即可。如果不需要演示，直接开始使用动态链接库，则选择VISpineWidget进行构建即可。其中spine-cpp子项目会构建为静态链接库（spine-cpp.lib），随后VISpineWidget会被构建为动态链接库（Visindigo-Widgets-SpineWidget.dll，Demo则动态链接该动态链接进行演示。
3. 有关演示项目，需要将一个有效的Spine 4.2资源放置在`src/VISpineWidgetDemo/examples/your_spine`目录下，并将其命名为`your_spine.atlas`、`your_spine.png`、`your_spine.skel`。演示项目会在运行时加载这些资源进行演示——考虑到版权问题，这个项目没有携带任何有效Spine资源。
4. 完成VISpineWidget的构建之后，只需携带Visindigo-Widgets-SpineWidget.dll及其lib，以及VISpineWidget/Widgets/spinewidget.h三个文件即可在任意Qt项目中使用SpineWidget。不需要在另外携带spine-cpp的源代码或其构建产物。

# 致谢
Visindigo Spine 是在 Github 开发者 tommego 的 [qspine](https://github.com/tommego/qspine) 
项目基础上，历经大型重构，使其从Qt Quick迁移到Qt Widgets的衍生项目。

我们也要同步的对原项目致谢的另外一个更古早的项目进行致谢，这是Qt框架下我们能找到的最早的实现：
[spine-qml](https://github.com/irukandji/spine-qml)

原项目目标平台为：
1. Qt 5.12+ 及其 Qt Quick 环境。
2. 支持Spine 3.x，理应是3.8。
3. 使用QtPro提供项目数据。
4. 理应不依赖特定的IDE或编译器，也不依赖特定操作系统和平台。

此项目重构之后的目标平台为：
1. Qt 6.10+ 及其 Qt Widgets 环境。
2. 支持Spine 4.2，理论最低4.0。
3. 使用Visual Studio 2026提供项目数据。
4. 在Windows_AMD64上提供完整功能。部分功能在其他平台缺失。

由于Visindigo项目现在的开发需求，此项目只提供Visual Studio 2026解决方案，不提供QtProject文件或CMakeLists.txt文件。

# 特性 和 未实现内容
此项目只进行了版本升级和目标迁移。除了为适配Spine3 -> Spine4 对动画系统的升级而允许混合多个轨道外，其他功能理应与原项目保持一致（或有部分缺失），以下为原项目声明支持的特性：

- real time timescale
- real time fps
- real time animation mixing
- real time skin setting
- real time animation setting
- real time skeleton scaling(extractly a global scale of <scaleX, scaleY>)
- async loading
- debug bones
- debug slots
- shared image texture
- clipping effect
- full stack multithread support
- window, linux, arm/arm64 cross compile project handle
- support realtime customized blend color on rgba/gray channel
- light control

以下为原项目声明未实现的特性：
- deug vertices
- VertexEffect
- audio trigger
- prealphamultiply blending support
- astc compressed texture support

# 贡献和维护
此仓库的重构和修复工作均主要由AI完成，Visindigo项目组仅进行了必要的引导和测试。

对于此项目，项目组在维护时将只保证其API和ABI稳定性，保证初始发布仓库时功能的稳定性，不对任何未来的功能更新做出担保。