#include "SpineViewer/SpineViewerFileServer.h"
#include "SpineViewer/SpineViewerWidget.h"
namespace YSS::SpineViewer {
	SpineViewerFileServer::SpineViewerFileServer(YSSCore::Editor::EditorPlugin* parent) :
		YSSCore::Editor::FileServer("SpineViewer", "cn.yxgeneral.yssspineviewer.fs", parent) {
		setEditorType(EditorType::BuiltInEditor);
		setSupportedFileExts({ "skel", "atlas" });
	}

	SpineViewerFileServer::~SpineViewerFileServer() {}

	YSSCore::Editor::FileEditWidget* SpineViewerFileServer::onCreateFileEditWidget() {
		return new SpineViewerWidget();
	}
}
