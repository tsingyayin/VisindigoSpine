#include "Plugin_YSSSpineViewer.h"
#include "SpineViewer/SpineViewerFileServer.h"
namespace YSS::SpineViewer {
	Translator::Translator(Visindigo::General::Plugin* parent) :
		Visindigo::General::Translator(parent, "YSSSpineViewer")
	{
		setDefaultLang(zh_CN);
		// 与 ASERStudio 保持一致的多语言支持
		addLangFilePath(zh_CN, ":/resource/cn.yxgeneral.yssspineviewer/i18n/zh_CN.json");
		addLangFilePath(zh_TW, ":/resource/cn.yxgeneral.yssspineviewer/i18n/zh_TW.json");
		addLangFilePath(en, ":/resource/cn.yxgeneral.yssspineviewer/i18n/en.json");
		addLangFilePath(ja, ":/resource/cn.yxgeneral.yssspineviewer/i18n/ja.json");
		addLangFilePath(jp_less_loanword, ":/resource/cn.yxgeneral.yssspineviewer/i18n/jp_less_loanword.json");
		addLangFilePath(ko, ":/resource/cn.yxgeneral.yssspineviewer/i18n/ko.json");
		addLangFilePath(ru, ":/resource/cn.yxgeneral.yssspineviewer/i18n/ru.json");
		addLangFilePath(de, ":/resource/cn.yxgeneral.yssspineviewer/i18n/de.json");
		addLangFilePath(fr, ":/resource/cn.yxgeneral.yssspineviewer/i18n/fr.json");
	}

	Main::Main() : YSSCore::Editor::EditorPlugin("cn.yxgeneral.yssspineviewer") {
		setPluginVersion(Compiled_VIAPI_Version);
		setPluginName("YSS Spine Viewer");
		setPluginAuthor({ "Tsing Yayin" });
	}
	Main::~Main() {}
	void Main::onPluginEnable() {
		registerPluginModule(new Translator(this));
		registerFileServer(new SpineViewerFileServer(this));
	}
	void Main::onApplicationInit() {}
	void Main::onPluginDisable() {}
	void Main::onTest() {}
	void Main::onProjectOpen(YSSCore::General::YSSProject* project) {
		Q_UNUSED(project)
	}
	void Main::onProjectClose(YSSCore::General::YSSProject* project) {
		Q_UNUSED(project)
	}
}
