#ifndef SPINEWIDGET_P_H
#define SPINEWIDGET_P_H

#include "spinewidget.h"

#include <QObject>
#include <QTimerEvent>
#include <QImage>
#include <QOpenGLTexture>
#include <QHash>
#include <QMutex>
#include <spine/spine.h>

namespace Visindigo::Widgets {

class SpineWidget;

static bool gTextureFreezed = false;

class Texture {
public:
    explicit Texture(const QString& name): name(name) {}
    ~Texture() { if (glTexture) { delete glTexture; glTexture = nullptr; } }
    QString name;
    QImage image;
    QOpenGLTexture* glTexture = nullptr;
};

class AimyTextureLoader: public spine::TextureLoader {
public:
    AimyTextureLoader();
    ~AimyTextureLoader() override;
    static AimyTextureLoader* instance();
    virtual void load(spine::AtlasPage& page, const spine::String& path) override;
    virtual void unload(void* texture) override;
    Texture* getGLTexture(Texture* texture);
    void releaseTextures();
    void invalidateGlTextures();

private:
    QHash<QString, Texture*> textureHash;
    QMutex mutex;
};

class AimyExtension: public spine::DefaultSpineExtension {
public:
    AimyExtension();
    virtual ~AimyExtension() override;

protected:
    virtual char* _readFile(const spine::String& path, int* length) override;
};

/**
 * Internal worker that runs animation updates (and skeleton loading) on a
 * background thread. All heavy CPU work happens here; GL drawing stays on the
 * GUI thread in SpineWidget::paintGL().
 *
 * Lives in the private header so the public API does not expose it.
 *
 * \since Visindigo 0.16.2
 */
class SpineWidgetWorker: public QObject {
    Q_OBJECT
public:
    SpineWidgetWorker(QObject* parent, SpineWidget* spineWidget);
    ~SpineWidgetWorker() override;

public slots:
    void loadResource();
    void updateSkeletonAnimation();
    void applyScale();
    void setAnimation(qint32 trackIndex, const QString& name, bool loop);
    void addAnimation(qint32 trackIndex, const QString& name, bool loop, float delay);
    void setSkin(const QString& skinName);
    void setToSetupPose();
    void setBonesToSetupPose();
    void setSlotsToSetupPose();
    void setAttachment(const QString& slotName, const QString& attachmentName);
    void setMix(const QString& fromAnimation, const QString& toAnimation, float duration);
    void clearTracks();
    void clearTrack(qint32 trackIndex);
    void kickAutoTick();   // 自适应模式: 幂等 kick 自驱动循环(worker 线程执行)

protected:
    void timerEvent(QTimerEvent* event) override;

private:
    SpineWidget* spineWidget;
    qint32 reloadTimerId = 0;
    bool needReloadResource = false;
    qint32 fadeCounter = 1;
    bool tickQueued = false;   // 自适应模式: 是否已排队下一帧(防重, 仅 worker 线程访问)
    void scheduleNextTick();   // 自适应模式: 排队下一帧自驱动调用(仅 worker 线程访问)
};

} // namespace Visindigo::Widgets

#endif // SPINEWIDGET_P_H
