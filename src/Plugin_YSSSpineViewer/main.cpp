#include "Plugin_YSSSpineViewer.h"
#include "YSSSpineViewerCompileMacro.h"
extern "C" YSSSpineViewerAPI Visindigo::General::Plugin* VisindigoPluginMain() {
	return new YSS::SpineViewer::Main();
}
