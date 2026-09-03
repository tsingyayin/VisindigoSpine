#ifndef YSS_SpineViewer_SpineViewerFileServer_H
#define YSS_SpineViewer_SpineViewerFileServer_H
#include <Editor/FileServer.h>
#include "YSSSpineViewerCompileMacro.h"
namespace YSS::SpineViewer {
	/*!
		\class YSS::SpineViewer::SpineViewerFileServer
		\inmodule YSSSpineViewer
		\brief Spine 骨骼资源文件服务器。

		支持 .skel(二进制骨架) 与 .atlas(图集描述) 两种扩展名,
		在 YSS 资源管理器中双击此类文件时创建 SpineViewerWidget 进行预览。
	*/
	class YSSSpineViewerAPI SpineViewerFileServer :public YSSCore::Editor::FileServer {
	public:
		SpineViewerFileServer(YSSCore::Editor::EditorPlugin* plugin);
		virtual ~SpineViewerFileServer();
		virtual YSSCore::Editor::FileEditWidget* onCreateFileEditWidget() override;
	};
}
#endif // YSS_SpineViewer_SpineViewerFileServer_H
