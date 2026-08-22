#ifndef SPINEWIDGET_H
#define SPINEWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QColor>
#include <QSize>
#include <QPoint>
#include <QStringList>

#include <QtGlobal>
class QShowEvent;
#ifndef VisindigoAPI
#  ifdef _MSC_VER
#    define VisindigoAPI Q_DECL_IMPORT
#    ifdef Visindigo_DEVELOPMENT
#      undef VisindigoAPI
#      define VisindigoAPI
#    endif
#    ifdef Visindigo_EXPORT
#      undef VisindigoAPI
#      define VisindigoAPI Q_DECL_EXPORT
#    endif
#  else
#    define VisindigoAPI
#  endif
#endif

namespace Visindigo::Widgets {
class SpineWidget;
class SpineWidgetPrivate;
class Texture;

class VisindigoAPI SpineWidget: public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
    Q_PROPERTY(QString atlasFile READ atlasFile WRITE setAtlasFile NOTIFY atlasFileChanged)
    Q_PROPERTY(QString skeletonFile READ skeletonFile WRITE setSkeletonFile NOTIFY skeletonFileChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(bool isSkeletonReady READ isSkeletonReady NOTIFY isSkeletonReadyChanged)
    Q_PROPERTY(QSize sourceSize READ sourceSize NOTIFY sourceSizeChanged)
    Q_PROPERTY(bool debugBones READ debugBones WRITE setDebugBones NOTIFY debugBonesChanged)
    Q_PROPERTY(bool debugSlots READ debugSlots WRITE setDebugSlots NOTIFY debugSlotsChanged)
    Q_PROPERTY(bool debugMesh READ debugMesh WRITE setDebugMesh NOTIFY debugMeshChanged)
    Q_PROPERTY(QStringList animations READ animations NOTIFY animationsChanged)
    Q_PROPERTY(QStringList skins READ skins NOTIFY skinsChanged)
    Q_PROPERTY(qreal skeletonScale READ skeletonScale WRITE setSkeletonScale NOTIFY skeletonScaleChanged)
    Q_PROPERTY(qreal timeScale READ timeScale WRITE setTimeScale NOTIFY timeScaleChanged)
    Q_PROPERTY(qint32 fps READ fps WRITE setFps NOTIFY fpsChanged)
    Q_PROPERTY(bool autoFpsEnable READ isAutoFpsEnable WRITE setAutoFpsEnable NOTIFY autoFpsEnableChanged)
    Q_PROPERTY(qreal defaultMix READ defaultMix WRITE setDefaultMix NOTIFY defaultMixChanged)
    Q_PROPERTY(qreal scaleX READ scaleX WRITE setScaleX NOTIFY scaleXChanged)
    Q_PROPERTY(qreal scaleY READ scaleY WRITE setScaleY NOTIFY scaleYChanged)
    Q_PROPERTY(QColor blendColor READ blendColor WRITE setBlendColor NOTIFY blendColorChanged)
    Q_PROPERTY(qint32 blendColorChannel READ blendColorChannel WRITE setBlendColorChannel NOTIFY blendColorChannelChanged)
    Q_PROPERTY(float light READ light WRITE setLight NOTIFY lightChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY backgroundColorChanged)
signals:
    void atlasFileChanged(const QString& path);
    void skeletonFileChanged(const QString& path);
    void loadedChanged(bool loaded);
    void isSkeletonReadyChanged(bool ready);
    void sourceSizeChanged(const QSize& size);
    void debugBonesChanged(bool debugBones);
    void debugSlotsChanged(bool debugSlots);
    void debugMeshChanged(bool debugMesh);
    void animationsChanged(const QStringList& animations);
    void skinsChanged(const QStringList& skins);
    void skeletonScaleChanged(const qreal& scale);
    void timeScaleChanged(const qreal& timeScale);
    void fpsChanged(qint32 fps);
    void autoFpsEnableChanged(bool enable);
    void autoFpsChanged(double fps);
    void defaultMixChanged(const qreal& defaultMix);
    void scaleXChanged(const qreal& scaleX);
    void scaleYChanged(const qreal& scaleY);
    void blendColorChanged(const QColor& color);
    void blendColorChannelChanged(qint32 channel);
    void lightChanged(float light);
    void backgroundColorChanged(const QColor& color);
    void animationUpdated();
    void resourceReady();
    void resourceLoadFailed();
    void preferSizeChanged(const QPointF& size);
    void animationStarted(qint32 trackId, const QString& animationName);
    void animationCompleted(qint32 trackId, const QString& animationName);
    void animationInterrupted(qint32 trackId, const QString& animationName);
    void animationEnded(qint32 trackId, const QString& animationName);
    void animationDisposed(qint32 trackId, const QString& animationName);

public:
    explicit SpineWidget(QWidget* parent = nullptr);
    ~SpineWidget() override;

public:
    QString atlasFile() const;
    void setAtlasFile(const QString& path);
    QString skeletonFile() const;
    void setSkeletonFile(const QString& path);
    bool loaded() const;
    bool isSkeletonReady() const;
    void reload();

    void setToSetupPose();
    void setBonesToSetupPose();
    void setSlotsToSetupPose();
    bool setAttachment(const QString& slotName, const QString& attachmentName);
    void setMix(const QString& fromAnimation, const QString& toAnimation, float duration);
    void setAnimation(qint32 trackIndex, const QString& name, bool loop);
    void addAnimation(qint32 trackIndex, const QString& name, bool loop, float delay = 0);
    void setSkin(const QString& skinName);
    void clearTracks();
    void clearTrack(qint32 trackIndex = 0);
    QStringList animations() const;
    QStringList skins() const;
    QString currentSkin() const;
    QString gpuRenderer() const;
    QString gpuVendor() const;
    QSize sourceSize() const;
    QPointF preferSize() const;

    qreal skeletonScale() const;
    void setSkeletonScale(const qreal& value);
    qreal timeScale() const;
    void setTimeScale(const qreal& value);
    qint32 fps() const;
    void setFps(qint32 fps);
    bool isAutoFpsEnable() const;
    void setAutoFpsEnable(bool enable);
    double getAutoFps() const;
    qreal defaultMix() const;
    void setDefaultMix(const qreal& value);
    qreal scaleX() const;
    void setScaleX(const qreal& value);
    qreal scaleY() const;
    void setScaleY(const qreal& value);
    QPointF anchorOffset() const;
    void setAnchorOffset(const QPointF& offset);
    bool clickThrough() const;
    void setClickThrough(bool clickThrough);
    bool hitTest(const QPoint& widgetPos) const;
    QColor blendColor() const;
    void setBlendColor(const QColor& color);
    qint32 blendColorChannel() const;
    void setBlendColorChannel(qint32 channel);
    float light() const;
    void setLight(float light);
    bool debugBones() const;
    void setDebugBones(bool debugBones);
    bool debugSlots() const;
    void setDebugSlots(bool debugSlots);
    bool debugMesh() const;
    void setDebugMesh(bool debugMesh);
    QColor backgroundColor() const;
    void setBackgroundColor(const QColor& color);

protected:
    void initializeGL() override;
    void resizeGL(qint32 w, qint32 h) override;
    void paintGL() override;
    void showEvent(QShowEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    friend class SpineWidgetWorker;
    friend class SpineWidgetPrivate;
    Q_DISABLE_COPY(SpineWidget)
    void startAnimationDriver();
    SpineWidgetPrivate* d;
};

} // namespace Visindigo::Widgets

#endif // SPINEWIDGET_H
