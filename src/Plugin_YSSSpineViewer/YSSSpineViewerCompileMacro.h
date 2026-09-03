#ifndef YSSSpineViewer_YSSSpineViewerCompileMacro_h
#define YSSSpineViewer_YSSSpineViewerCompileMacro_h
#define YSSSpineViewerAPI __declspec(dllimport)
#ifdef YSSSPINEVIEWER_DEVELOPMENT
#undef YSSSpineViewerAPI
#define YSSSpineViewerAPI
#endif
#ifdef YSSSPINEVIEWER_EXPORT
#undef YSSSpineViewerAPI
#define YSSSpineViewerAPI __declspec(dllexport)
#endif
#endif // YSSSpineViewer_YSSSpineViewerCompileMacro_h
