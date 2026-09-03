#ifndef YSS_Plugin_YSSSpineViewer_H
#define YSS_Plugin_YSSSpineViewer_H
#include <Editor/EditorPlugin.h>
#include <General/Translator.h>
namespace YSS::SpineViewer {
	class Translator :public Visindigo::General::Translator {
	public:
		Translator(Visindigo::General::Plugin* parent);
	};

	/*!
		\class YSS::SpineViewer::Main
		\inmodule YSSSpineViewer
		\brief YSS Spine 预览器插件主类。

		将 VISpineWidgetDemo 的功能移植为 YSS 插件:
		- 注册 SpineViewerFileServer, 支持在项目中直接打开 .skel/.atlas 骨骼文件进行预览
		  (文件一律经 YSS 文件服务器打开, 不提供独立工具窗口/打开按钮)
	*/
	class Main : public YSSCore::Editor::EditorPlugin {
		Q_OBJECT;
	public:
		Main();
		virtual ~Main();
		virtual void onPluginEnable() override;
		virtual void onApplicationInit() override;
		virtual void onPluginDisable() override;
		virtual void onProjectOpen(YSSCore::General::YSSProject* project) override;
		virtual void onProjectClose(YSSCore::General::YSSProject* project) override;
		virtual void onTest() override;
	};
}
#endif // YSS_Plugin_YSSSpineViewer_H
