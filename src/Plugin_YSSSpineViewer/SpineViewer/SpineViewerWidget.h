#ifndef YSS_SpineViewer_SpineViewerWidget_H
#define YSS_SpineViewer_SpineViewerWidget_H
#include <Editor/FileEditWidget.h>
class QGridLayout;
class QLabel;
class QSlider;
class QCheckBox;
class QButtonGroup;
class QTimer;
class QPushButton;
class QEvent;

namespace Visindigo::Widgets { class SpineWidget; class BorderFrame; }

namespace YSS::SpineViewer {
	class SpineViewerWidgetPrivate;

	/*!
		\class YSS::SpineViewer::SpineViewerWidget
		\inmodule YSSSpineViewer
		\brief Spine 骨骼动画预览器(功能改编自 VISpineWidgetDemo 的 DemoWindow)。

	布局: 左侧竖条(重置姿态/自适应帧率 + 调试 + 背景), 中间 SpineWidget 渲染区, 右侧控制面板。
	右侧自上而下: 加载信息、皮肤、动画、属性。
	- 动画: 上半"已应用到轨道"(BorderFrame 圆角边框, 表头 动画名|循环|层级; 每行 = [动画名 | 循环勾 | 上移↑ | 下移↓], 双击行移回) + 下半"选择动画"(BorderFrame 圆角边框, 2 列 PushButton 网格, 点击添加);
	  已应用顺序 = 轨道号(第0行=track 0 基础, 越大越上层叠加)
	- 皮肤: 互斥 RadioButton(当前皮肤唯一)
	- 重置姿态/自适应帧率(左竖条顶部)
	- 调试(左竖条): 骨骼/插槽/网格 线框开关
	- 背景(左竖条): 黑色/灰色/白色/自定义 切换(垂直布局)
	- 属性: FPS、缩放、时间倍率、X/Y 缩放、原点偏移、混合时长
		自动解析同名的另一份资源(atlas/skel)后交给 SpineWidget 加载。
		文件一律由 YSS 通过文件服务器打开(不提供独立"打开文件"按钮, 避免破坏
		YSS 对文件标签页的封装)。

		\note 原 Demo 的无边框点击穿透属顶层窗口特性, 在编辑器内不适用, 已移除。
	*/
	class SpineViewerWidget : public YSSCore::Editor::FileEditWidget {
		Q_OBJECT
	public:
		explicit SpineViewerWidget(QWidget* parent = nullptr);
		virtual ~SpineViewerWidget() override;
	public:
		virtual bool onOpen(const QString& filePath) override;
		virtual bool onClose() override;
		virtual bool onSave(const QString& filePath = "") override;
		bool loadSpineFiles(const QString& skelPath, const QString& atlasPath);
	protected:
		bool eventFilter(QObject* obj, QEvent* ev) override;
	private slots:
		void onResourceReady();
		void onLoadFailed();
		void updateStatus();
		void addToApplied(const QString& name);
		void removeFromApplied(QWidget* row);
		void syncAppliedTracks();
	private:
		QSlider* makeSlider(int from, int to, double value);
		QWidget* makeAppliedRow(const QString& name, bool loop);
		void moveAppliedRow(int from, int to);
		void updateAppliedContainerSize();
		void rebuildAnimationAndSkinControls();
		void rebuildAvailableGrid();
		SpineViewerWidgetPrivate* d;
	};
}
#endif // YSS_SpineViewer_SpineViewerWidget_H
