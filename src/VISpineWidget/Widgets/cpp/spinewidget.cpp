#include "spinewidget.h"
#include "spinewidget_p.h"   // 私有头: SpineWidgetWorker(moc 在 DLL 内部, 不对外发布)

#include <spine/spine.h>   // PIMPL: spine 依赖只在本 cpp(公共头不再 include spine)
#ifdef Q_OS_WIN
#  include <windows.h>
#endif
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLFunctions>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QTimerEvent>
#include <QFile>
#include <QDebug>
#include <QCursor>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMatrix4x4>
#include <QVector3D>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>
#include <QElapsedTimer>
#include <QShowEvent>
#include <vector>
#include <float.h>
namespace Visindigo::Widgets {

static const char kTextureVertSrc[] = R"(#version 330 core

uniform highp mat4 u_matrix;

out vec4 v_color;
out vec2 v_texCoord;

in vec2 a_position;
in vec2 a_texCoord;
in vec4 a_color;

void main() {
   gl_Position = u_matrix * vec4(a_position.xy, 0.0, 1.0);
   v_color = a_color;
   v_texCoord = a_texCoord;
}

)";
static const char kTextureFragSrc[] = R"(#version 330 core

in vec4 v_color;
uniform vec4 u_blendColor;
uniform int u_blendColorChannel;
in vec2 v_texCoord;
uniform sampler2D u_texture;
uniform float u_light;
out vec4 fragColor;

void main() {
   vec4 t_color = texture(u_texture, v_texCoord);
   vec4 ret_color = v_color * t_color;
   if(u_blendColorChannel == 0){
       ret_color = vec4(ret_color.r, ret_color.r, ret_color.r, ret_color.a) * u_blendColor;
   }
   else if(u_blendColorChannel == 1) {
       ret_color = vec4(ret_color.g, ret_color.g, ret_color.g, ret_color.a) * u_blendColor;
   }
   else if(u_blendColorChannel == 2) {
       ret_color = vec4(ret_color.b, ret_color.b, ret_color.b, ret_color.a) * u_blendColor;
   }
   else if(u_blendColorChannel == 3) {
       ret_color = vec4(ret_color.a, ret_color.a, ret_color.a, ret_color.a) * u_blendColor;
   }
   else if(u_blendColorChannel == 4) {
       float gray = ret_color.r * 0.299 + ret_color.g * 0.587 + ret_color.b * 0.114;
       ret_color = vec4(gray, gray, gray, ret_color.a) * u_blendColor;
   }
   ret_color = ret_color * vec4(u_light, u_light, u_light, 1.0);
   ret_color.rgb *= ret_color.a; // premultiplied alpha: 贴图为 straight(未预乘), 混合为 premultiplied, 半透明附件(面纱等)需预乘 RGB
   fragColor = ret_color;
}

)";
static const char kColorVertSrc[] = R"(#version 330 core

in vec2 a_position;
uniform vec4 u_color;
uniform highp mat4 u_matrix;
out vec4 v_color;

void main() {
   gl_Position = u_matrix * vec4(a_position.xy, 0.0, 1.0);
   v_color = u_color;
}

)";
static const char kColorFragSrc[] = R"(#version 330 core

in vec4 v_color;
out vec4 fragColor;

void main() {
   fragColor = v_color;
}

)";

// ---- 内部渲染结构 ----
namespace {
struct SpineVertex {
    float x, y;
    float u, v;
    spine::Color color;
};
struct RenderCmdBatch {
    spine::Vector<SpineVertex> vertices;
    spine::Vector<GLushort> triangles;
    Texture* texture = nullptr;
    qint32 blendMode = 0;
};
/*
    \brief 点在三角形内测试(重心坐标法)。
    \a p 测试点(世界坐标)
    \a a b c 三角形顶点
    \a u v w 输出重心坐标(u+v+w==1)
    \return true=点在三角形内(含边界)
*/
bool pointInTriangle(const QVector3D& p, const SpineVertex& a, const SpineVertex& b,
                     const SpineVertex& c, float& u, float& v, float& w)
{
    const float ax = b.x - a.x, ay = b.y - a.y;
    const float bx = c.x - a.x, by = c.y - a.y;
    const float px = p.x() - a.x, py = p.y() - a.y;
    const float denom = ax * by - ay * bx;
    if (qAbs(denom) < 1e-12f)
        return false;
    const float invDenom = 1.0f / denom;
    v = (px * by - py * bx) * invDenom;
    w = (ax * py - ay * px) * invDenom;
    u = 1.0f - v - w;
    return v >= 0.0f && w >= 0.0f && v + w <= 1.0f;
}

/*
    \brief 采样图集贴图在归一化 UV 处的 alpha, 判断是否"不透明"。
    \a texture 贴图对象
    \a u v 归一化 UV [0,1]
    \return true=不透明
*/
bool sampleTextureAlpha(const Texture* texture, float u, float v)
{
    if (not texture || texture->image.isNull())
        return false;
    const QImage& img = texture->image;
    const qint32 px = qBound(0, qRound(u * (img.width() - 1)), img.width() - 1);
    const qint32 py = qBound(0, qRound(v * (img.height() - 1)), img.height() - 1);
    switch (img.format()) {
    case QImage::Format_ARGB32:
    case QImage::Format_ARGB32_Premultiplied:
    case QImage::Format_RGBA8888:
    case QImage::Format_RGBA8888_Premultiplied:
    case QImage::Format_RGBX8888:
        return img.constScanLine(py)[px * 4 + 3] > 0;
    default:
        return true;   // 未知格式(无透明通道)保守视为命中
    }
}

/*
    \brief 统计皮肤包含的附件条目数(判断皮肤是否有实际内容)。
    \a skin 皮肤
    \return 附件条目数
*/
size_t countSkinAttachments(spine::Skin* skin)
{
    if (not skin)
        return 0;
    size_t count = 0;
    spine::Skin::AttachmentMap::Entries entries = skin->getAttachments();
    while (entries.hasNext()) {
        entries.next();
        count++;
    }
    return count;
}
}

// ==================== SpineWidgetPrivate (定义) ====================
/*
    \class SpineWidgetPrivate
    \brief PIMPL 私有实现: 持有 SpineWidget 的全部私有数据与 spine 相关逻辑。
    定义在本 cpp, 公共头不暴露, 因此使用方无需 spine 头文件。
*/
class SpineWidgetPrivate {
public:
    friend class SpineWidget;
    friend class SpineWidgetWorker;
    static SpineWidgetPrivate* of(SpineWidget* w) { return w->d; }
    void notifyAnimationEvent(spine::EventType type, qint32 track, const QString& name);

protected:
    explicit SpineWidgetPrivate(SpineWidget* spineWidget): q(spineWidget) {}
    ~SpineWidgetPrivate() { delete[] worldVertices; }

    SpineWidget* q;

    QString atlasFile;
    QString skeletonFile;
    QStringList animations;
    QStringList skins;
    QString currentSkin;   // 当前应用皮肤名(loadResource/setSkin 同步更新)
    bool loaded = false;
    bool debugBones = false;
    bool debugSlots = false;
    bool debugMesh = false;
    qreal debugBonesWidth = 2.0;    // 骨骼调试线宽(默认 2)
    qreal debugSlotsWidth = 1.0;    // 插槽调试线宽(默认 1)
    qreal debugMeshWidth = 1.0;     // 网格调试线宽(默认 1)
    QColor debugBonesColor = QColor(200, 150, 40, 255);   // 与旧渲染颜色保持一致
    QColor debugSlotsColor = QColor(200, 40, 150, 255);
    QColor debugMeshColor = QColor(40, 150, 200, 255);
    bool isLoading = false;
    bool hasViewport = false;
    bool animating = false;
    bool glReady = false;
    QString gpuRenderer;   // 当前实际 GL 渲染器(多 GPU 诊断用, initializeGL 后有效)
    QString gpuVendor;     // 当前实际 GL 厂商(多 GPU 诊断用)
    QOpenGLFunctions* gl = nullptr;
    qreal scaleX = 1.0;
    qreal scaleY = 1.0;
    QPointF anchorOffset;
    bool interactEnable = true;   // 鼠标交互开关(滚轮缩放/左键拖动平移)
    bool dragging = false;        // 是否正在左键拖动
    QPointF lastDragPos;          // 上次拖动位置(控件坐标)
    bool clickThrough = false;
    qreal timeScale = 1.0;
    qreal skeletonScale = 1.0;
    float light = 1.0;
    qint32 fps = 25;
    bool autoFpsEnable = false;      // 自适应帧率开关(开启=能跑多快跑多快)
    double autoFps = 0.0;            // 当前实际帧率(0=未开启/未测得)
    qreal defaultMix = 0.1;
    QColor blendColor = QColor(255, 255, 255, 255);
    QColor backgroundColor = QColor(0, 0, 0, 0);
    qint32 blendColorChannel = -1;
    QSize sourceSize;
    float* worldVertices = nullptr;
    size_t worldVerticesCapacity = 0;
    QRectF boundingRect;
    QRectF renderRect;
    QRectF viewportRect;
    bool boundsValid = false;   // 包围盒是否已有效(依赖动画附件的用例需首帧后补算)
    QElapsedTimer timer;

    QTimer* lazyLoadTimer = nullptr;
    QTimer* animTimer = nullptr;
    spine::Atlas* atlas = nullptr;
    spine::SkeletonData* skeletonData = nullptr;
    spine::AnimationStateData* animationStateData = nullptr;
    spine::AnimationState* animationState = nullptr;
    spine::Skeleton* skeleton = nullptr;
    spine::SkeletonClipping* clipper = nullptr;
    SpineWidgetWorker* worker = nullptr;
    QThread* workerThread = nullptr;
    // 帧缓冲轮转复用容量: worker 构建 scratch -> 发布到 batches -> GUI 取走 renderSnapshot 渲染;
    // 三者均对齐到槽位数, 轮转间不销毁 spine::Vector 容量, 避免每帧堆分配; 渲染期间不持锁。
    std::vector<RenderCmdBatch> batches;
    std::vector<RenderCmdBatch> scratch;
    std::vector<RenderCmdBatch> renderSnapshot;
    RenderCmdBatch slotBatch;   // 单槽临时批次(复用容量)
    size_t batchCount = 0;      // batches 中有效批次数量
    size_t renderCount = 0;     // renderSnapshot 中有效批次数量
    QMutex batchesMutex;

    QOpenGLShaderProgram* textureProgram = nullptr;
    QOpenGLShaderProgram* colorProgram = nullptr;

    QRectF computeBoundingRect();
    Texture* getTexture(spine::Attachment* attachment) const;
    void releaseSkeletonRelatedData();
    bool nothingToDraw(spine::Slot& slot);
    void batchRenderCmd();
    void initShaders();
    void ensureGlTexture(Texture* texture);
    void renderBatches();
    void renderDebug();
    void applyScaleInternal();
    void loadResource();
    QMatrix4x4 projectionMatrix() const;
    bool hitTest(const QPointF& widgetPos);
};

/*
    \brief 将 QString 转换为 spine::String(UTF-8)。
    \a str 输入字符串
    \return spine::String
*/
static spine::String qstringtospinestring(const QString& str)
{
    return spine::String(str.toUtf8().constData());
}

/*
    \brief spine 动画事件回调: 转发为 SpineWidget 的信号。
    \a state 动画状态机
    \a type 事件类型
    \a entry 轨道项
    \a event 事件(可能为空)
*/
static void animationStateListener(spine::AnimationState* state, spine::EventType type, spine::TrackEntry* entry, spine::Event* event)
{
    Q_UNUSED(event)
    SpineWidget* spineWidget = static_cast<SpineWidget*>(state->getRendererObject());
    if (not spineWidget)
        return;
    SpineWidgetPrivate* d = SpineWidgetPrivate::of(spineWidget);
    d->notifyAnimationEvent(type, entry->getTrackIndex(),
                            QString::fromUtf8(entry->getAnimation()->getName().buffer()));
}

// ==================== SpineWidgetWorker (实现) ====================
/*
    \brief 构造 worker。
    \a parent 父对象
    \a spineWidget 关联的 SpineWidget(用于访问其私有实现)
*/
SpineWidgetWorker::SpineWidgetWorker(QObject* parent, SpineWidget* spineWidget)
    : QObject(parent), spineWidget(spineWidget)
{
}

/*
    \brief 析构: 清理残留的延迟重载定时器。
*/
SpineWidgetWorker::~SpineWidgetWorker()
{
    if (reloadTimerId) {
        killTimer(reloadTimerId);
        reloadTimerId = 0;
    }
}

/*
    \brief 更新一帧骨架动画(worker 线程调用): 推进动画、应用变换、重建渲染批次。
*/
void SpineWidgetWorker::updateSkeletonAnimation()
{
    tickQueued = false;   // 本帧已开始处理, 队列中不再有自驱动调用
    SpineWidgetPrivate* d = spineWidget->d;
    if (not spineWidget->isSkeletonReady()) {
        qWarning() << "SpineWidget::updateSkeletonAnimation(): skeleton is not ready";
        return;
    }
    if (d->animationState->getTracks().size() <= 0 || not spineWidget->isVisible()) {
        if (fadeCounter > 0)
            fadeCounter--;
        else
            return;
    } else
        fadeCounter = 1;

    const qint64 nsecs = d->timer.isValid() ? d->timer.nsecsElapsed() : 0;
    d->timer.restart();
    const float deltaTime = nsecs / 1e9 * static_cast<float>(d->timeScale);

    d->animationState->update(deltaTime);
    d->animationState->apply(*d->skeleton);
    d->skeleton->updateWorldTransform(spine::Physics_None);

    // 依赖动画附件的用例: setup 无附件, 动画首帧才把附件挂上, 此时一次性补算包围盒
    if (not d->boundsValid) {
        d->renderRect = d->computeBoundingRect();
        if (d->renderRect.width() > 0 && d->renderRect.height() > 0) {
            d->boundsValid = true;
            emit spineWidget->preferSizeChanged(spineWidget->preferSize());
        }
    }

    d->batchRenderCmd();
    emit spineWidget->animationUpdated();

    // 自适应帧率: 测量实际帧率并"算完立即再算下一帧"自驱动, 能跑多快跑多快。
    // 关闭时(或本帧 early-return 时)不再排队, 循环自然停止, 由 animTimer 接管固定帧率。
    if (d->autoFpsEnable) {
        if (nsecs > 0) {
            const double instFps = 1e9 / double(nsecs);
            d->autoFps = d->autoFps <= 0 ? instFps : d->autoFps + (instFps - d->autoFps) * 0.1;
            emit spineWidget->autoFpsChanged(d->autoFps);
        }
        scheduleNextTick();
    }
}

/*
    \brief 自适应模式 kick: 若无排队帧则排队一帧(worker 线程执行, 幂等)。
*/
void SpineWidgetWorker::kickAutoTick()
{
    if (spineWidget->d->autoFpsEnable)
        scheduleNextTick();
}

/*
    \brief 排队下一帧自驱动调用(仅 worker 线程调用, tickQueued 防重避免双跑)。
*/
void SpineWidgetWorker::scheduleNextTick()
{
    if (tickQueued)
        return;
    tickQueued = true;
    QMetaObject::invokeMethod(this, "updateSkeletonAnimation", Qt::QueuedConnection);
}

/*
    \brief 应用缩放(worker 线程调用): 触发私有实现重算并重绘。
*/
void SpineWidgetWorker::applyScale()
{
    if (not spineWidget->isSkeletonReady())
        return;
    spineWidget->d->applyScaleInternal();
    fadeCounter = 1;
    updateSkeletonAnimation();
}

/*
    \brief 加载 atlas + skeleton 资源并重建骨架(worker 线程调用)。
*/
void SpineWidgetWorker::loadResource()
{
    SpineWidgetPrivate* d = spineWidget->d;
    d->releaseSkeletonRelatedData();
    d->isLoading = true;
    d->loaded = false;
    d->animations.clear();
    d->skins.clear();

    emit spineWidget->loadedChanged(d->loaded);
    emit spineWidget->isSkeletonReadyChanged(spineWidget->isSkeletonReady());

    if (d->atlasFile.isEmpty()) {
        qWarning() << "SpineWidget: atlasFile is empty";
        emit spineWidget->resourceLoadFailed();
        d->isLoading = false;
        return;
    }
    if (d->skeletonFile.isEmpty()) {
        qWarning() << "SpineWidget: skeletonFile is empty";
        emit spineWidget->resourceLoadFailed();
        d->isLoading = false;
        return;
    }

    d->atlas = new spine::Atlas(qstringtospinestring(d->atlasFile), AimyTextureLoader::instance());
    if (d->atlas->getPages().size() == 0) {
        qWarning() << "Failed to load atlas:" << d->atlasFile;
        emit spineWidget->resourceLoadFailed();
        d->isLoading = false;
        return;
    }

    QString skeletonPath = d->skeletonFile;
    // Auto-detect .skel / .json (assets may only ship one of the two)
    if (not QFile::exists(skeletonPath)) {
        if (skeletonPath.endsWith(QLatin1String(".json"), Qt::CaseInsensitive)) {
            const QString alt = skeletonPath.left(skeletonPath.size() - 5) + QLatin1String(".skel");
            if (QFile::exists(alt)) skeletonPath = alt;
        } else if (skeletonPath.endsWith(QLatin1String(".skel"), Qt::CaseInsensitive)) {
            const QString alt = skeletonPath.left(skeletonPath.size() - 5) + QLatin1String(".json");
            if (QFile::exists(alt)) skeletonPath = alt;
        }
    }

    const bool isBinarySkeleton = skeletonPath.endsWith(QLatin1String(".skel"), Qt::CaseInsensitive);
    if (isBinarySkeleton) {
        spine::SkeletonBinary binary(d->atlas);
        binary.setScale(1);
        d->skeletonData = binary.readSkeletonDataFile(qstringtospinestring(skeletonPath));
        if (not d->skeletonData) {
            qWarning() << "Failed to load skeleton (.skel):" << binary.getError().buffer();
            emit spineWidget->resourceLoadFailed();
            d->isLoading = false;
            return;
        }
    } else {
        spine::SkeletonJson json(d->atlas);
        json.setScale(1);
        d->skeletonData = json.readSkeletonDataFile(qstringtospinestring(skeletonPath));
        if (not d->skeletonData) {
            qWarning() << "Failed to load skeleton (.json):" << json.getError().buffer();
            emit spineWidget->resourceLoadFailed();
            d->isLoading = false;
            return;
        }
    }

    d->skeleton = new spine::Skeleton(d->skeletonData);
    d->skeleton->setX(0);
    d->skeleton->setY(0);

    // 按最大附件顶点数调整 worldVertices 容量, 避免固定缓冲越界
    size_t maxWorldVertices = 2000;
    for (size_t i = 0; i < d->skeleton->getSlots().size(); i++) {
        auto* attachment = d->skeleton->getSlots()[i]->getAttachment();
        if (attachment && attachment->getRTTI().isExactly(spine::MeshAttachment::rtti))
            maxWorldVertices = qMax(maxWorldVertices,
                                    static_cast<size_t>(static_cast<spine::MeshAttachment*>(attachment)->getWorldVerticesLength()));
    }
    if (maxWorldVertices > d->worldVerticesCapacity) {
        delete[] d->worldVertices;
        d->worldVertices = new float[maxWorldVertices];
        d->worldVerticesCapacity = maxWorldVertices;
    }

    d->animationStateData = new spine::AnimationStateData(d->skeletonData);
    d->animationStateData->setDefaultMix(d->defaultMix);
    d->animationState = new spine::AnimationState(d->animationStateData);
    d->animationState->setRendererObject(spineWidget);
    d->animationState->setListener(animationStateListener);

    auto animations = d->skeletonData->getAnimations();
    for (size_t i = 0; i < animations.size(); i++) {
        d->animations << QString::fromUtf8(animations[i]->getName().buffer());
    }
    emit spineWidget->animationsChanged(d->animations);

    auto skins = d->skeletonData->getSkins();
    for (size_t i = 0; i < skins.size(); i++) {
        d->skins << QString::fromUtf8(skins[i]->getName().buffer());
    }

    d->loaded = true;
    d->isLoading = false;

    // 初始皮肤: 选附件最多的皮肤(default 常为 setup/空皮肤, 如本用例 default 仅 1 个附件,
    // 实际角色附件在 Self/Clone 里), 保证加载后即有可见内容。
    spine::Skin* initialSkin = nullptr;
    size_t bestCount = 0;
    for (size_t i = 0; i < d->skeletonData->getSkins().size(); i++) {
        spine::Skin* s = d->skeletonData->getSkins()[i];
        size_t c = countSkinAttachments(s);
        if (c > bestCount) { bestCount = c; initialSkin = s; }
    }
    if (initialSkin)
        d->skeleton->setSkin(initialSkin);
    d->currentSkin = initialSkin
            ? QString::fromUtf8(initialSkin->getName().buffer()) : QString();

    d->skeleton->updateWorldTransform(spine::Physics_None);
    // 依赖动画附件的用例 setup 无附件, 包围盒需待动画首帧计算; 此处先尝试,
    // 无效则保持空(不渲染), 由 updateSkeletonAnimation 首帧有附件后一次性补算。
    d->renderRect = d->computeBoundingRect();
    d->boundsValid = d->renderRect.width() > 0 && d->renderRect.height() > 0;
    if (not d->boundsValid)
        d->renderRect = QRectF();   // 无内容则保持空, 等首帧动画补算

    emit spineWidget->preferSizeChanged(spineWidget->preferSize());

    spineWidget->d->applyScaleInternal();
    d->skeleton->updateWorldTransform(spine::Physics_None);
    d->boundingRect = d->computeBoundingRect();

    emit spineWidget->skinsChanged(d->skins);
    emit spineWidget->loadedChanged(d->loaded);
    emit spineWidget->isSkeletonReadyChanged(spineWidget->isSkeletonReady());

    d->batchRenderCmd();
    QThread::msleep(1);
    emit spineWidget->animationUpdated();
    emit spineWidget->resourceReady();
}

/*
    \brief 在指定轨道设置动画并立即播放(worker 线程调用)。
    \a trackIndex 轨道索引(>=0)
    \a name 动画名(须存在于 animations())
    \a loop 是否循环播放
*/
void SpineWidgetWorker::setAnimation(qint32 trackIndex, const QString& name, bool loop)
{
    SpineWidgetPrivate* d = spineWidget->d;
    if (not spineWidget->isSkeletonReady()) {
        qWarning() << "SpineWidget::setAnimation Error: Skeleton is not ready";
        return;
    }
    if (not d->animations.contains(name)) {
        qWarning() << "no" << name << "found, available is:" << d->animations;
        return;
    }
    d->animationState->setAnimation(size_t(trackIndex), qstringtospinestring(name), loop);
    d->timer.restart();
    // 自适应模式: 设置动画后确保自驱动循环继续/启动
    if (d->autoFpsEnable)
        scheduleNextTick();
}

/*
    \brief 在指定轨道末尾排队动画(当前动画播完后再播)(worker 线程调用)。
    \a trackIndex 轨道索引
    \a name 动画名
    \a loop 是否循环
    \a delay 延迟秒数
*/
void SpineWidgetWorker::addAnimation(qint32 trackIndex, const QString& name, bool loop, float delay)
{
    SpineWidgetPrivate* d = spineWidget->d;
    if (not spineWidget->isSkeletonReady()) {
        qWarning() << "SpineWidget::addAnimation Error: Skeleton is not ready";
        return;
    }
    if (not d->animations.contains(name)) {
        qWarning() << "no" << name << "found, available is:" << d->animations;
        return;
    }
    d->animationState->addAnimation(size_t(trackIndex), qstringtospinestring(name), loop, delay);
    d->timer.restart();
    // 自适应模式: 排队动画后确保自驱动循环继续/启动
    if (d->autoFpsEnable)
        scheduleNextTick();
}

/*
    \brief 恢复整个骨架到 Setup Pose(worker 线程调用)。
*/
void SpineWidgetWorker::setToSetupPose()
{
    SpineWidgetPrivate* d = spineWidget->d;
    if (not spineWidget->isSkeletonReady()) {
        qWarning() << "SpineWidget::setToSetupPose Error: Skeleton is not ready";
        return;
    }
    d->skeleton->setToSetupPose();
    d->animationState->apply(*d->skeleton);
    d->skeleton->updateWorldTransform(spine::Physics_None);
}

/*
    \brief 仅恢复骨骼到 Setup Pose(worker 线程调用)。
*/
void SpineWidgetWorker::setBonesToSetupPose()
{
    SpineWidgetPrivate* d = spineWidget->d;
    if (not spineWidget->isSkeletonReady()) {
        qWarning() << "SpineWidget::setBonesToSetupPose Error: Skeleton is not ready";
        return;
    }
    d->skeleton->setBonesToSetupPose();
}

/*
    \brief 仅恢复插槽/附件到 Setup Pose(worker 线程调用)。
*/
void SpineWidgetWorker::setSlotsToSetupPose()
{
    SpineWidgetPrivate* d = spineWidget->d;
    if (not spineWidget->isSkeletonReady()) {
        qWarning() << "SpineWidget::setSlotsToSetupPose Error: Skeleton is not ready";
        return;
    }
    d->skeleton->setSlotsToSetupPose();
}

/*
    \brief 替换指定插槽上的附件(worker 线程调用)。
    \a slotName 插槽名称
    \a attachmentName 附件名称
*/
void SpineWidgetWorker::setAttachment(const QString& slotName, const QString& attachmentName)
{
    SpineWidgetPrivate* d = spineWidget->d;
    if (not spineWidget->isSkeletonReady()) {
        qWarning() << "SpineWidget::setAttachment Error: Skeleton is not ready";
        return;
    }
    d->skeleton->setAttachment(qstringtospinestring(slotName), qstringtospinestring(attachmentName));
}

/*
    \brief 设置两动画间混合时长(worker 线程调用)。
    \a fromAnimation 源动画名
    \a toAnimation 目标动画名
    \a duration 混合时长(秒)
*/
void SpineWidgetWorker::setMix(const QString& fromAnimation, const QString& toAnimation, float duration)
{
    SpineWidgetPrivate* d = spineWidget->d;
    if (not spineWidget->isSkeletonReady()) {
        qWarning() << "SpineWidget::setMix Error: Skeleton is not ready";
        return;
    }
    d->animationStateData->setMix(qstringtospinestring(fromAnimation), qstringtospinestring(toAnimation), duration);
}

/*
    \brief 设置皮肤(worker 线程调用)。
    \a skinName 皮肤名
*/
void SpineWidgetWorker::setSkin(const QString& skinName)
{
    SpineWidgetPrivate* d = spineWidget->d;
    if (not spineWidget->isSkeletonReady()) {
        qWarning() << "SpineWidget::setSkin Error: Skeleton is not ready";
        return;
    }
    qInfo() << "setting skin" << skinName;
    if (not d->skins.contains(skinName)) {
        qWarning() << "no" << skinName << "found, available skins is:" << d->skins;
        return;
    }
    d->skeleton->setSkin(qstringtospinestring(skinName));
    d->currentSkin = skinName;
    // 皮肤会改变骨骼激活集与各槽附件, 需重建世界变换与渲染批次并触发重绘
    d->skeleton->updateWorldTransform(spine::Physics_None);
    d->batchRenderCmd();
    emit spineWidget->animationUpdated();
}

/*
    \brief 清空所有轨道动画(worker 线程调用)。
*/
void SpineWidgetWorker::clearTracks()
{
    SpineWidgetPrivate* d = spineWidget->d;
    if (not spineWidget->isSkeletonReady()) {
        qWarning() << "SpineWidget::clearTracks Error: Skeleton is not ready";
        return;
    }
    d->animationState->clearTracks();
    d->animating = false;
}

/*
    \brief 清空指定轨道动画(worker 线程调用)。
    \a trackIndex 轨道索引
*/
void SpineWidgetWorker::clearTrack(qint32 trackIndex)
{
    SpineWidgetPrivate* d = spineWidget->d;
    if (not spineWidget->isSkeletonReady()) {
        qWarning() << "SpineWidget::clearTrack Error: Skeleton is not ready";
        return;
    }
    d->animationState->clearTrack(size_t(trackIndex));
    if (d->animationState->getTracks().size() <= 0)
        d->animating = false;
}

/*
    \brief 延迟重载定时器回调: 若标记了需要重载则加载资源。
    \a event 定时器事件
*/
void SpineWidgetWorker::timerEvent(QTimerEvent* event)
{
    if (reloadTimerId == event->timerId() && needReloadResource) {
        needReloadResource = false;
        loadResource();
        killTimer(reloadTimerId);
        reloadTimerId = 0;
    }
}

// ==================== SpineWidgetPrivate (实现) ====================
/*
    \brief 动画事件转发为 SpineWidget 信号(由 animationStateListener 调用)。
    \a type 事件类型
    \a track 轨道索引
    \a name 动画名
*/
void SpineWidgetPrivate::notifyAnimationEvent(spine::EventType type, qint32 track, const QString& name)
{
    switch (type) {
    case spine::EventType_Start:
        emit q->animationStarted(track, name);
        if (not animating) {
            animating = true;
            emit q->animationUpdated();
        }
        break;
    case spine::EventType_Interrupt:
        emit q->animationInterrupted(track, name);
        break;
    case spine::EventType_End:
        emit q->animationEnded(track, name);
        break;
    case spine::EventType_Complete:
        emit q->animationCompleted(track, name);
        break;
    case spine::EventType_Dispose:
        emit q->animationDisposed(track, name);
        break;
    case spine::EventType_Event:
        break;
    }
}

/*
    \brief 懒加载定时器入口: 投递资源加载任务到 worker 线程。
*/
void SpineWidgetPrivate::loadResource()
{
    if (workerThread->isRunning() && worker)
        QMetaObject::invokeMethod(worker, "loadResource");
}

/*
    \brief 编译着色器程序(纹理 + 纯色)。
*/
void SpineWidgetPrivate::initShaders()
{
    if (textureProgram) { delete textureProgram; textureProgram = nullptr; }
    if (colorProgram) { delete colorProgram; colorProgram = nullptr; }

    textureProgram = new QOpenGLShaderProgram;
    bool texOk = textureProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, kTextureVertSrc)
            && textureProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, kTextureFragSrc)
            && textureProgram->link();
    if (not texOk)
        qWarning() << "SpineWidget texture shader error:" << textureProgram->log();

    colorProgram = new QOpenGLShaderProgram;
    bool colOk = colorProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, kColorVertSrc)
            && colorProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, kColorFragSrc)
            && colorProgram->link();
    if (not colOk)
        qWarning() << "SpineWidget color shader error:" << colorProgram->log();
}

/*
    \brief 确保贴图已创建对应 GL 纹理(懒创建)。
    \a texture 贴图对象
*/
void SpineWidgetPrivate::ensureGlTexture(Texture* texture)
{
    if (not texture || texture->glTexture || texture->image.isNull())
        return;
    texture->glTexture = new QOpenGLTexture(texture->image, QOpenGLTexture::DontGenerateMipMaps);
    texture->glTexture->setMinificationFilter(QOpenGLTexture::Linear);
    texture->glTexture->setMagnificationFilter(QOpenGLTexture::Linear);
    texture->glTexture->setWrapMode(QOpenGLTexture::ClampToEdge);
    // 记录创建时的上下文, 供销毁时安全判定(见 Texture::~Texture / invalidateGlTextures)
    texture->context = QOpenGLContext::currentContext();
}

/*
    \brief 应用缩放的内部入口: 仅触发一次重绘(实际变换由渲染投影矩阵完成)。
*/
void SpineWidgetPrivate::applyScaleInternal()
{
    emit q->animationUpdated();
}

/*
    \brief 计算骨架所有附件的世界坐标包围盒。
    \return 世界坐标边界矩形
*/
QRectF SpineWidgetPrivate::computeBoundingRect()
{
    if (not q->isSkeletonReady())
        return QRectF();
    float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
    float vminX = FLT_MAX, vminY = FLT_MAX, vmaxX = -FLT_MAX, vmaxY = -FLT_MAX;
    for (size_t i = 0; i < skeleton->getSlots().size(); i++) {
        auto slot = skeleton->getSlots()[i];
        if (not slot->getAttachment())
            continue;
        size_t verticesCount;
        auto* attachment = slot->getAttachment();
        if (attachment->getRTTI().isExactly(spine::RegionAttachment::rtti)) {
            auto* regionAttachment = static_cast<spine::RegionAttachment*>(slot->getAttachment());
            regionAttachment->computeWorldVertices(*slot, worldVertices, 0, 2);
            verticesCount = 8;
            if (QString(attachment->getName().buffer()).endsWith("viewport")) {
                hasViewport = true;
                for (size_t ii = 0; ii < verticesCount; ii += 2) {
                    float x = worldVertices[ii], y = worldVertices[ii + 1];
                    vminX = qMin(vminX, x); vminY = qMin(vminY, y);
                    vmaxX = qMax(vmaxX, x); vmaxY = qMax(vmaxY, y);
                }
                viewportRect = QRectF(vminX, vminY, vmaxX - vminX, vmaxY - vminY);
            }
        } else if (attachment->getRTTI().isExactly(spine::MeshAttachment::rtti)) {
            auto* meshAttachment = static_cast<spine::MeshAttachment*>(slot->getAttachment());
            verticesCount = meshAttachment->getWorldVerticesLength();
            meshAttachment->computeWorldVertices(*slot, worldVertices);
            if (QString(attachment->getName().buffer()).endsWith("viewport")) {
                hasViewport = true;
                for (size_t ii = 0; ii < verticesCount; ii += 2) {
                    float x = worldVertices[ii], y = worldVertices[ii + 1];
                    vminX = qMin(vminX, x); vminY = qMin(vminY, y);
                    vmaxX = qMax(vmaxX, x); vmaxY = qMax(vmaxY, y);
                }
                viewportRect = QRectF(vminX, vminY, vmaxX - vminX, vmaxY - vminY);
            }
        } else
            continue;
        for (size_t ii = 0; ii < verticesCount; ii += 2) {
            float x = worldVertices[ii], y = worldVertices[ii + 1];
            minX = qMin(minX, x); minY = qMin(minY, y);
            maxX = qMax(maxX, x); maxY = qMax(maxY, y);
        }
    }
    return QRectF(minX, minY, maxX - minX, maxY - minY);
}

/*
    \brief 从附件取对应贴图对象。
    \a attachment 附件
    \return 贴图对象; 不支持的类型返回 nullptr
*/
Texture* SpineWidgetPrivate::getTexture(spine::Attachment* attachment) const
{
    if (attachment->getRTTI().isExactly(spine::RegionAttachment::rtti)) {
        return static_cast<Texture*>(static_cast<spine::AtlasRegion*>(static_cast<spine::RegionAttachment*>(attachment)->getRegion())->page->texture);
    } else if (attachment->getRTTI().isExactly(spine::MeshAttachment::rtti)) {
        return static_cast<Texture*>(static_cast<spine::AtlasRegion*>(static_cast<spine::MeshAttachment*>(attachment)->getRegion())->page->texture);
    }
    return nullptr;
}

/*
    \brief 释放骨架相关数据(atlas/skeletonData/animationState/skeleton), 供重载与析构使用。
*/
void SpineWidgetPrivate::releaseSkeletonRelatedData()
{
    hasViewport = false;
    delete animationState; animationState = nullptr;
    delete animationStateData; animationStateData = nullptr;
    delete skeleton; skeleton = nullptr;
    delete skeletonData; skeletonData = nullptr;
    delete atlas; atlas = nullptr;
    loaded = false;
}

/*
    \brief 判断插槽当前是否无可绘制内容(无附件/骨骼未激活/alpha 为 0)。
    \a slot 插槽
    \return true=无需绘制
*/
bool SpineWidgetPrivate::nothingToDraw(spine::Slot& slot)
{
    auto attachment = slot.getAttachment();
    if (not attachment || not slot.getBone().isActive() || slot.getColor().a == 0)
        return true;
    if (attachment->getRTTI().isExactly(spine::RegionAttachment::rtti)) {
        if (static_cast<spine::RegionAttachment*>(attachment)->getColor().a == 0)
            return true;
    } else if (attachment->getRTTI().isExactly(spine::MeshAttachment::rtti)) {
        if (static_cast<spine::MeshAttachment*>(attachment)->getColor().a == 0)
            return true;
    }
    return false;
}

static const GLushort quadIndices[] = {0, 1, 2, 2, 3, 0};

/*
    \brief 遍历插槽构建渲染批次(region/mesh 顶点 + 三角形 + 贴图), 并处理裁剪。
*/
void SpineWidgetPrivate::batchRenderCmd()
{
    const size_t slotCount = skeleton->getSlots().size();
    // 一次性把三块缓冲对齐到槽位数, 之后轮转复用容量, 避免每帧堆分配
    if (scratch.size() < slotCount || batches.size() < slotCount || renderSnapshot.size() < slotCount) {
        scratch.resize(slotCount);
        batches.resize(slotCount);
        renderSnapshot.resize(slotCount);
    }

    size_t used = 0;                 // 合并后的有效批次数量
    RenderCmdBatch* openBatch = nullptr;   // 当前可继续合并的目标批次

    for (size_t i = 0, n = slotCount; i < n; ++i) {
        auto slot = skeleton->getDrawOrder()[i];

        if (nothingToDraw(*slot)) {
            clipper->clipEnd(*slot);
            continue;
        }

        auto* attachment = slot->getAttachment();
        if (not attachment)
            continue;

        // 复用单槽临时批次容量(spine::Vector::clear 仅置 size=0, 保留容量)
        slotBatch.vertices.clear();
        slotBatch.triangles.clear();
        Texture* texture = nullptr;
        qint32 blendMode = slot->getData().getBlendMode();

        auto skeletonColor = skeleton->getColor();
        auto slotColor = slot->getColor();
        spine::Color attachmentColor(0, 0, 0, 0);
        spine::Color tint(skeletonColor.r * slotColor.r,
                          skeletonColor.g * slotColor.g,
                          skeletonColor.b * slotColor.b,
                          skeletonColor.a * slotColor.a);

        if (attachment->getRTTI().isExactly(spine::RegionAttachment::rtti)) {
            auto regionAttachment = static_cast<spine::RegionAttachment*>(attachment);
            attachmentColor.set(regionAttachment->getColor());
            tint.set(tint.r * attachmentColor.r, tint.g * attachmentColor.g,
                     tint.b * attachmentColor.b, tint.a * attachmentColor.a);
            texture = getTexture(regionAttachment);
            slotBatch.vertices.setSize(4, SpineVertex());
            regionAttachment->computeWorldVertices(*slot,
                                                   (float*)slotBatch.vertices.buffer(),
                                                   0,
                                                   sizeof(SpineVertex) / sizeof(float));
            for (size_t j = 0, l = 0; j < 4; j++, l += 2) {
                auto& vertex = slotBatch.vertices[j];
                vertex.color.set(tint);
                vertex.u = regionAttachment->getUVs()[l];
                vertex.v = regionAttachment->getUVs()[l + 1];
            }
            slotBatch.triangles.setSize(6, 0);
            memcpy(slotBatch.triangles.buffer(), quadIndices, 6 * sizeof(GLushort));
        } else if (attachment->getRTTI().isExactly(spine::MeshAttachment::rtti)) {
            auto mesh = static_cast<spine::MeshAttachment*>(attachment);
            attachmentColor.set(mesh->getColor());
            tint.set(tint.r * attachmentColor.r, tint.g * attachmentColor.g,
                     tint.b * attachmentColor.b, tint.a * attachmentColor.a);
            size_t numVertices = mesh->getWorldVerticesLength() / 2;
            slotBatch.vertices.setSize(numVertices, SpineVertex());
            texture = getTexture(mesh);
            mesh->computeWorldVertices(*slot,
                                       0,
                                       mesh->getWorldVerticesLength(),
                                       (float*)slotBatch.vertices.buffer(),
                                       0,
                                       sizeof(SpineVertex) / sizeof(float));
            for (size_t j = 0, l = 0; j < numVertices; j++, l += 2) {
                auto& vertex = slotBatch.vertices[j];
                vertex.color.set(tint);
                vertex.u = mesh->getUVs()[l];
                vertex.v = mesh->getUVs()[l + 1];
            }
            slotBatch.triangles.setSize(mesh->getTriangles().size(), 0);
            memcpy(slotBatch.triangles.buffer(), mesh->getTriangles().buffer(),
                   mesh->getTriangles().size() * sizeof(GLushort));
        } else if (attachment->getRTTI().isExactly(spine::ClippingAttachment::rtti)) {
            auto clip = static_cast<spine::ClippingAttachment*>(attachment);
            clipper->clipStart(*slot, clip);
            continue;
        } else {
            continue;
        }

        if (clipper->isClipping()) {
            auto& clippedVertices = clipper->getClippedVertices();
            auto& clippedTriangles = clipper->getClippedTriangles();
            auto& clippedUVs = clipper->getClippedUVs();

            spine::Vector<float> tmpVertices;
            spine::Vector<float> tmpUvs;
            size_t tmpVerticesCount = slotBatch.vertices.size() * 2;
            tmpVertices.setSize(tmpVerticesCount, 0);
            tmpUvs.setSize(tmpVerticesCount, 0);

            for (size_t j = 0; j < slotBatch.vertices.size(); j++) {
                tmpVertices[j * 2] = slotBatch.vertices[j].x;
                tmpVertices[j * 2 + 1] = slotBatch.vertices[j].y;
                tmpUvs[j * 2] = slotBatch.vertices[j].u;
                tmpUvs[j * 2 + 1] = slotBatch.vertices[j].v;
            }
            clipper->clipTriangles(tmpVertices.buffer(), slotBatch.triangles.buffer(),
                                   slotBatch.triangles.size(), tmpUvs.buffer(), sizeof(qint16));
            tmpVertices.setSize(0, 0);
            tmpUvs.setSize(0, 0);

            if (clippedTriangles.size() == 0) {
                clipper->clipEnd(*slot);
                continue;
            }
            size_t vertCount = clippedVertices.size() / 2;
            slotBatch.triangles.setSize(clippedTriangles.size(), 0);
            memcpy(slotBatch.triangles.buffer(), clippedTriangles.buffer(), clippedTriangles.size() * sizeof(GLushort));
            slotBatch.vertices.setSize(vertCount, SpineVertex());
            for (size_t j = 0; j < vertCount; j++) {
                slotBatch.vertices[j].x = clippedVertices[j * 2];
                slotBatch.vertices[j].y = clippedVertices[j * 2 + 1];
                slotBatch.vertices[j].u = clippedUVs[j * 2];
                slotBatch.vertices[j].v = clippedUVs[j * 2 + 1];
                slotBatch.vertices[j].color.set(tint);
            }
        }
        slotBatch.texture = texture;
        slotBatch.blendMode = blendMode;

        // 合并: 与上一批次同贴图且同混合模式(且不超 16 位索引上限)则追加顶点/三角形, 减少 draw call
        const bool canMerge = openBatch
                && openBatch->texture == texture
                && openBatch->blendMode == blendMode
                && openBatch->vertices.size() + slotBatch.vertices.size() <= 65536;
        if (canMerge) {
            const size_t vertOffset = openBatch->vertices.size();
            const size_t oldTriSize = openBatch->triangles.size();
            openBatch->vertices.setSize(vertOffset + slotBatch.vertices.size(), SpineVertex());
            memcpy(openBatch->vertices.buffer() + vertOffset, slotBatch.vertices.buffer(),
                   slotBatch.vertices.size() * sizeof(SpineVertex));
            openBatch->triangles.setSize(oldTriSize + slotBatch.triangles.size(), 0);
            for (size_t t = 0; t < slotBatch.triangles.size(); ++t)
                openBatch->triangles[oldTriSize + t] =
                        static_cast<GLushort>(slotBatch.triangles[t] + vertOffset);
        } else {
            openBatch = &scratch[used++];
            openBatch->vertices.clear();
            openBatch->triangles.clear();
            openBatch->texture = texture;
            openBatch->blendMode = blendMode;
            openBatch->vertices.setSize(slotBatch.vertices.size(), SpineVertex());
            memcpy(openBatch->vertices.buffer(), slotBatch.vertices.buffer(),
                   slotBatch.vertices.size() * sizeof(SpineVertex));
            openBatch->triangles.setSize(slotBatch.triangles.size(), 0);
            memcpy(openBatch->triangles.buffer(), slotBatch.triangles.buffer(),
                   slotBatch.triangles.size() * sizeof(GLushort));
        }
        clipper->clipEnd(*slot);
    }
    clipper->clipEnd();

    // 发布: 仅交换所有权(不拷贝), 加锁时间极短, 不阻塞 GUI 渲染
    {
        QMutexLocker locker(&batchesMutex);
        batches.swap(scratch);
        batchCount = used;
    }
}

/*
    \brief 计算渲染用投影矩阵(cover 等比适配 + 锚点偏移 + 缩放/翻转)。
    渲染与命中测试共用此矩阵, 保证两套坐标系严格一致。
    \return 投影矩阵; 内容为空时返回单位矩阵
*/
QMatrix4x4 SpineWidgetPrivate::projectionMatrix() const
{
    const QRectF contentRect = hasViewport ? viewportRect : (renderRect.isEmpty() ? boundingRect : renderRect);
    if (contentRect.isEmpty())
        return QMatrix4x4();
    const qreal vw = qMax<qreal>(1.0, q->width());
    const qreal vh = qMax<qreal>(1.0, q->height());
    const qreal scale = qMax(vw / contentRect.width(), vh / contentRect.height());
    const qreal viewW = vw / scale;
    const qreal viewH = vh / scale;
    const qreal ox = anchorOffset.x();   // 不再限制 -1~1(交互拖动/滑块可超出)
    const qreal oy = anchorOffset.y();
    QMatrix4x4 matrix;
    // 显式 ortho(left, right, bottom, top): 世界 Y 小(脚) -> 投影 bottom -> NDC 底部(-1);
    // 世界 Y 大(头) -> 投影 top -> NDC 顶部(+1)。注意不能用 QMatrix4x4::ortho(QRectF),
    // 它把 rect 当 Y-down 处理导致角色上下颠倒。
    matrix.ortho(-viewW * (1 + ox) / 2.0f, viewW * (1 - ox) / 2.0f,
                 -viewH * (1 - oy) / 2.0f, viewH * (1 + oy) / 2.0f,
                 -1.0f, 1.0f);
    // 缩放/翻转围绕世界原点(即 anchorOffset 指定的锚点): scaleY<0 时绕原点翻转
    matrix.scale(scaleX * skeletonScale, scaleY * skeletonScale, 1.0f);
    return matrix;
}

/*
    \brief 命中测试(无锁): 基于 GUI 线程独占的 renderSnapshot 做 CPU 几何命中。
    将控件坐标经投影矩阵的逆变换到世界坐标, 遍历渲染批次做点在三角形内测试,
    命中后用重心坐标插值 UV 到图集贴图采样 alpha。
    不变量: renderSnapshot/renderCount 仅由 GUI 线程(renderBatches/hitTest)读写,
    worker 线程只碰 scratch/batches, 两者互不重叠, 故无需加锁。
    \a widgetPos 控件本地逻辑坐标(左上角原点, Y 向下)
    \return true=骨架覆盖(不透明)
*/
bool SpineWidgetPrivate::hitTest(const QPointF& widgetPos)
{
    if (not q->isSkeletonReady())
        return false;
    const qint32 w = q->width();
    const qint32 h = q->height();
    if (widgetPos.x() < 0 || widgetPos.y() < 0 || widgetPos.x() >= w || widgetPos.y() >= h)
        return false;
    const QRectF contentRect = hasViewport ? viewportRect : (renderRect.isEmpty() ? boundingRect : renderRect);
    if (contentRect.isEmpty())
        return false;

    // 控件逻辑坐标 -> NDC -> 世界坐标(渲染同款投影矩阵的逆)
    const QMatrix4x4 inv = projectionMatrix().inverted();
    const float ndcX = float(2.0 * widgetPos.x() / w - 1.0);
    const float ndcY = float(1.0 - 2.0 * widgetPos.y() / h);
    const QVector3D world = inv.map(QVector3D(ndcX, ndcY, 0.0f));

    for (size_t i = 0; i < renderCount; ++i) {
        RenderCmdBatch& batch = renderSnapshot[i];
        if (not batch.texture || batch.triangles.size() < 3)
            continue;
        const size_t triCount = batch.triangles.size() / 3;
        for (size_t t = 0; t < triCount; ++t) {
            SpineVertex& v0 = batch.vertices[batch.triangles[t * 3]];
            SpineVertex& v1 = batch.vertices[batch.triangles[t * 3 + 1]];
            SpineVertex& v2 = batch.vertices[batch.triangles[t * 3 + 2]];
            // 三角形 AABB 快速剔除
            const float minX = qMin(v0.x, qMin(v1.x, v2.x));
            const float maxX = qMax(v0.x, qMax(v1.x, v2.x));
            const float minY = qMin(v0.y, qMin(v1.y, v2.y));
            const float maxY = qMax(v0.y, qMax(v1.y, v2.y));
            if (world.x() < minX || world.x() > maxX || world.y() < minY || world.y() > maxY)
                continue;
            float u, vv, ww;
            if (not pointInTriangle(world, v0, v1, v2, u, vv, ww))
                continue;
            const float tu = u * v0.u + vv * v1.u + ww * v2.u;
            const float tv = u * v0.v + vv * v1.v + ww * v2.v;
            if (sampleTextureAlpha(batch.texture, tu, tv))
                return true;
        }
    }
    return false;
}

/*
    \brief 提交渲染批次到 GL(纹理批次, 处理混合模式与调色/亮度)。
*/
void SpineWidgetPrivate::renderBatches()
{
    const QRectF contentRect = hasViewport ? viewportRect : (renderRect.isEmpty() ? boundingRect : renderRect);
    if (contentRect.isEmpty())
        return;

    // 投影矩阵与命中测试共用(见 projectionMatrix), 保证坐标系一致
    const QMatrix4x4 matrix = projectionMatrix();

    if (not textureProgram || not textureProgram->isLinked())
        return;

    // 取走最新帧(仅所有权交换), 渲染期间不持锁, 避免阻塞 worker 构建下一帧;
    // 无新帧时沿用上一帧, 避免空窗闪烁。
    {
        QMutexLocker locker(&batchesMutex);
        if (batchCount > 0) {
            renderSnapshot.swap(batches);
            renderCount = batchCount;
            batchCount = 0;
        }
    }

    textureProgram->bind();
    textureProgram->setUniformValue("u_matrix", matrix);
    textureProgram->enableAttributeArray("a_position");
    textureProgram->enableAttributeArray("a_color");
    textureProgram->enableAttributeArray("a_texCoord");
    // 帧级常量 uniform 只设置一次, 避免每批次重复提交
    textureProgram->setUniformValue("u_texture", 0);
    textureProgram->setUniformValue("u_blendColor", blendColor.redF(), blendColor.greenF(),
                                    blendColor.blueF(), blendColor.alphaF());
    textureProgram->setUniformValue("u_blendColorChannel", blendColorChannel);
    textureProgram->setUniformValue("u_light", light);

    gl->glEnable(GL_BLEND);
    QOpenGLTexture* lastGlTexture = nullptr;
    qint32 lastBlendMode = -1;
    for (size_t i = 0; i < renderCount; ++i) {
        RenderCmdBatch& batch = renderSnapshot[i];
        if (not batch.texture || batch.triangles.size() == 0)
            continue;

        // 混合模式/纹理未变化时跳过重复的 GL 状态设置
        if (batch.blendMode != lastBlendMode) {
            switch (batch.blendMode) {
            case spine::BlendMode_Additive: gl->glBlendFunc(GL_ONE, GL_ONE); break;
            // Multiply 的 dst 因子必须是 GL_ONE_MINUS_SRC_ALPHA:
            // 用 GL_ONE_MINUS_SRC_COLOR 时 dst = src*dst + dst*(1-src) = dst, 恒为无操作,
            // 半透明黑面纱(Multiply)会因此完全不可见。
            // 用 glBlendFuncSeparate: 颜色走 Multiply(dstColor), alpha 走 (ONE, 1-srcA),
            // 否则透明背景(0,0,0,0)下 alpha 会被乘成 0, 面纱透过透明窗体完全看不见。
            case spine::BlendMode_Multiply: gl->glBlendFuncSeparate(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
            case spine::BlendMode_Screen:   gl->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR); break;
            default:                        gl->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
            }
            lastBlendMode = batch.blendMode;
        }

        ensureGlTexture(batch.texture);
        if (batch.texture->glTexture != lastGlTexture) {
            gl->glActiveTexture(GL_TEXTURE0);
            batch.texture->glTexture->bind();
            lastGlTexture = batch.texture->glTexture;
        }

        textureProgram->setAttributeArray("a_position", GL_FLOAT, &batch.vertices[0].x, 2, sizeof(SpineVertex));
        textureProgram->setAttributeArray("a_color", GL_FLOAT, &batch.vertices[0].color.r, 4, sizeof(SpineVertex));
        textureProgram->setAttributeArray("a_texCoord", GL_FLOAT, &batch.vertices[0].u, 2, sizeof(SpineVertex));
        gl->glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(batch.triangles.size()), GL_UNSIGNED_SHORT, batch.triangles.buffer());
    }
    lastGlTexture = nullptr;
    lastBlendMode = -1;

    if (debugBones || debugSlots || debugMesh)
        renderDebug();
}

/*
    \brief 渲染调试辅助线(骨骼/插槽包围盒/网格)。
*/
void SpineWidgetPrivate::renderDebug()
{
    struct Point {
        Point(float x, float y): x(x), y(y) {}
        Point(): x(0.0f), y(0.0f) {}
        GLfloat x;
        GLfloat y;
    };

    if (not colorProgram || not colorProgram->isLinked())
        return;
    const QRectF contentRect = hasViewport ? viewportRect : (renderRect.isEmpty() ? boundingRect : renderRect);
    if (contentRect.isEmpty())
        return;
    // 与 renderBatches 一致: cover 等比 + 原点在 widget 中心(见 projectionMatrix)
    const QMatrix4x4 matrix = projectionMatrix();

    colorProgram->bind();
    colorProgram->setUniformValue("u_matrix", matrix);
    colorProgram->enableAttributeArray("a_position");
    gl->glDisable(GL_BLEND);

    if (debugSlots) {
        colorProgram->setUniformValue("u_color", debugSlotsColor);
        gl->glLineWidth(static_cast<GLfloat>(debugSlotsWidth));
        Point points[4];
        for (size_t i = 0, n = skeleton->getSlots().size(); i < n; i++) {
            auto slot = skeleton->getSlots()[i];
            if (not slot->getAttachment() || not slot->getAttachment()->getRTTI().isExactly(spine::RegionAttachment::rtti))
                continue;
            auto* regionAttachment = static_cast<spine::RegionAttachment*>(slot->getAttachment());
            regionAttachment->computeWorldVertices(*slot, worldVertices, 0, 2);
            points[0] = Point(worldVertices[0], worldVertices[1]);
            points[1] = Point(worldVertices[2], worldVertices[3]);
            points[2] = Point(worldVertices[4], worldVertices[5]);
            points[3] = Point(worldVertices[6], worldVertices[7]);
            colorProgram->setAttributeArray("a_position", GL_FLOAT, points, 2, sizeof(Point));
            gl->glDrawArrays(GL_LINE_LOOP, 0, 4);
        }
    }

    if (debugMesh) {
        colorProgram->setUniformValue("u_color", debugMeshColor);
        gl->glLineWidth(static_cast<GLfloat>(debugMeshWidth));
        spine::Vector<Point> world, lines;
        for (size_t i = 0, n = skeleton->getSlots().size(); i < n; i++) {
            auto slot = skeleton->getSlots()[i];
            if (not slot->getAttachment() || not slot->getAttachment()->getRTTI().isExactly(spine::MeshAttachment::rtti))
                continue;
            auto* mesh = static_cast<spine::MeshAttachment*>(slot->getAttachment());
            const size_t numVertices = mesh->getWorldVerticesLength() / 2;
            world.setSize(numVertices, Point());
            mesh->computeWorldVertices(*slot, 0, mesh->getWorldVerticesLength(),
                                       (float*)world.buffer(), 0, sizeof(Point) / sizeof(float));
            auto& triangles = mesh->getTriangles();
            const size_t base = lines.size();
            lines.setSize(base + (triangles.size() / 3) * 6, Point());
            size_t k = base;
            for (size_t t = 0; t + 2 < triangles.size(); t += 3) {
                for (size_t e = 0; e < 3; e++) {
                    lines[k++] = world[triangles[t + e]];
                    lines[k++] = world[triangles[t + (e + 1) % 3]];
                }
            }
        }
        if (lines.size() > 0) {
            colorProgram->setAttributeArray("a_position", GL_FLOAT, lines.buffer(), 2, sizeof(Point));
            gl->glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lines.size()));
        }
    }

    if (debugBones) {
        colorProgram->setUniformValue("u_color", debugBonesColor);
        gl->glLineWidth(static_cast<GLfloat>(debugBonesWidth));
        for (size_t i = 0, n = skeleton->getBones().size(); i < n; i++) {
            auto bone = skeleton->getBones()[i];
            if (not bone->isActive())
                continue;
            float x = bone->getData().getLength() * bone->getA() + bone->getWorldX();
            float y = bone->getData().getLength() * bone->getC() + bone->getWorldY();
            Point p0(bone->getWorldX(), bone->getWorldY());
            Point p1(x, y);
            Point line[2] = {p0, p1};
            colorProgram->setAttributeArray("a_position", GL_FLOAT, line, 2, sizeof(Point));
            gl->glDrawArrays(GL_LINES, 0, 2);
        }
    }
}

// ==================== SpineWidget (实现) ====================
/*!
    \class Visindigo::Widgets::SpineWidget
    \since Visindigo 0.16.2
    \brief 渲染 Spine 骨骼动画的 QtWidgets 控件(QOpenGLWidget 子类)。
    \inmodule Visindigo

    SpineWidget 将 spine-cpp 的骨骼加载与动画计算放在后台线程(SpineWidgetWorker)
    执行, GL 绘制在 GUI 线程的 paintGL() 中完成。默认透明背景, 适合作为角色/立绘
    显示在任意 QtWidgets 界面中。

    典型用法:
    \code
    auto* spine = new SpineWidget(this);
    spine->setAtlasFile("char.atlas");
    spine->setSkeletonFile("char.skel");   // 设置后延迟 50ms 自动在后台加载
    spine->setAnimation(0, "Idle", true);  // 播放循环动画
    \endcode

    渲染约定:
    - 坐标系: Spine 世界坐标 Y 轴向上(头在上、脚在下)。
    - 等比适配: cover 模式(内容填满控件, 超出部分裁剪, 不变形)。
    - 视图中心固定为世界原点 (0,0), 可用 setAnchorOffset() 平移。
    - 缩放/翻转通过投影矩阵围绕锚点完成, 负的 scaleX/scaleY 不会把骨架移出窗口。

    \note 所有动画控制接口均为线程安全, 内部投递到 worker 线程执行。

    \sa SpineWidgetPrivate, SpineWidgetWorker
*/

/*!
    \fn void SpineWidget::resourceReady()
    \since Visindigo 0.16.2
    资源加载成功, 骨架已可渲染。
    \sa resourceLoadFailed(), isSkeletonReady()
*/

/*!
    \fn void SpineWidget::resourceLoadFailed()
    \since Visindigo 0.16.2
    资源加载失败(路径错误/文件损坏等)。
    \sa resourceReady()
*/

/*!
    \fn void SpineWidget::animationUpdated()
    \since Visindigo 0.16.2
    骨架每完成一次动画更新后发射(worker 线程计算完一帧), 用于驱动外部重绘。
*/

/*!
    \fn void SpineWidget::preferSizeChanged(const QPointF &size)
    \since Visindigo 0.16.2
    内容自然尺寸(世界坐标宽高)改变时发射。
    \sa preferSize()
*/

/*!
    \fn void SpineWidget::animationStarted(int trackId, const QString &animationName)
    \since Visindigo 0.16.2
    指定轨道(track)上的动画开始播放。
    \a trackId 轨道索引
    \a animationName 动画名
    \sa setAnimation()
*/

/*!
    \fn void SpineWidget::animationCompleted(int trackId, const QString &animationName)
    \since Visindigo 0.16.2
    指定轨道上的循环动画完整播放一次。
    \a trackId 轨道索引
    \a animationName 动画名
    \sa animationStarted()
*/

/*!
    \fn void SpineWidget::animationInterrupted(int trackId, const QString &animationName)
    \since Visindigo 0.16.2
    指定轨道上的动画被另一个动画打断。
    \a trackId 轨道索引
    \a animationName 动画名
    \sa animationStarted()
*/

/*!
    \fn void SpineWidget::animationEnded(int trackId, const QString &animationName)
    \since Visindigo 0.16.2
    指定轨道上的动画播放结束(非循环动画)。
    \a trackId 轨道索引
    \a animationName 动画名
    \sa animationStarted()
*/

/*!
    \fn void SpineWidget::animationDisposed(int trackId, const QString &animationName)
    \since Visindigo 0.16.2
    指定轨道上的动画被释放(从动画状态机移除)。
    \a trackId 轨道索引
    \a animationName 动画名
    \sa animationStarted()
*/

/*!
    \since Visindigo 0.16.2
    构造 SpineWidget。

    创建后台 worker 线程、懒加载定时器与动画驱动定时器。
    GL 表面默认带 8bit alpha(配合透明背景)与 4x MSAA。
    资源通过 setAtlasFile()/setSkeletonFile() 设置, 设置后延迟 50ms 自动加载。

    \a parent 父控件

*/
SpineWidget::SpineWidget(QWidget* parent)
    : QOpenGLWidget(parent), d(new SpineWidgetPrivate(this))
{
    SpineWidgetPrivate* dd = d;
    // shader 已内嵌为字符串(见文件顶部常量), 不再使用 qrc: 共享库中的 qrc 自动注册
    // 会在 DLL 加载的 loader lock 下调用 qRegisterResourceData 导致死锁
    // 透明背景支持: GL 帧缓冲必须带 alpha 通道
    QSurfaceFormat fmt = format();
    fmt.setAlphaBufferSize(8);
    fmt.setSamples(4);
    setFormat(fmt);
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);

    AimyTextureLoader::instance(); // ensure initialized

    dd->clipper = new spine::SkeletonClipping;
    dd->lazyLoadTimer = new QTimer;
    dd->worker = new SpineWidgetWorker(nullptr, this);
    dd->workerThread = new QThread;
    dd->worldVertices = new float[2000];

    dd->lazyLoadTimer->setSingleShot(true);
    dd->lazyLoadTimer->setInterval(50);

    // 槽用 lambda(this 上下文)间接调 private 的 loadResource, 使公共头零私有入口
    connect(dd->lazyLoadTimer, &QTimer::timeout, this, [this]() { d->loadResource(); });
    connect(this, &SpineWidget::animationUpdated, this, [this]() { update(); });

    // 动画驱动: 周期性通知 worker 更新骨架动画(worker 内部按 fps 用 msleep 自限帧,
    // 并 emit animationUpdated -> 本控件 update() 重绘)。设置动画时启动(见 setAnimation)。
    dd->animTimer = new QTimer;
    dd->animTimer->setTimerType(Qt::PreciseTimer);
    dd->animTimer->setInterval(1000 / qMax(1, dd->fps));
    connect(dd->animTimer, &QTimer::timeout, this, [this]() {
        SpineWidgetPrivate* dd2 = d;
        if (dd2->workerThread->isRunning() && dd2->worker)
            QMetaObject::invokeMethod(dd2->worker, "updateSkeletonAnimation");
    });
    dd->worker->moveToThread(dd->workerThread);
    dd->workerThread->start();
}

/*!
    \since Visindigo 0.16.2
    析构。

    断开信号连接、停止并等待 worker 线程退出, 在 GL 上下文仍有效时释放
    着色器程序并失效所有纹理。

*/
SpineWidget::~SpineWidget()
{
    SpineWidgetPrivate* dd = d;
    disconnect(dd->lazyLoadTimer, &QTimer::timeout, this, nullptr);
    disconnect(this, &SpineWidget::animationUpdated, this, nullptr);

    if (dd->animationState)
        dd->animationState->clearTracks();
    dd->workerThread->quit();
    dd->workerThread->wait();
    delete dd->worker; dd->worker = nullptr;
    dd->releaseSkeletonRelatedData();

    // GL 资源需在上下文有效时释放 (QOpenGLWidget 上下文此时仍有效)
    makeCurrent();
    if (dd->textureProgram) { delete dd->textureProgram; dd->textureProgram = nullptr; }
    if (dd->colorProgram) { delete dd->colorProgram; dd->colorProgram = nullptr; }
    AimyTextureLoader::instance()->invalidateGlTextures();
    doneCurrent();

    delete dd->animTimer; dd->animTimer = nullptr;
    delete dd->lazyLoadTimer; dd->lazyLoadTimer = nullptr;
    delete dd->workerThread; dd->workerThread = nullptr;
    delete dd->clipper; dd->clipper = nullptr;
    delete d; d = nullptr;
    // worldVertices 由 SpineWidgetPrivate 析构统一释放
}

/*!
    \property SpineWidget::atlasFile
    \since Visindigo 0.16.2
    当前图集(atlas)文件路径(.atlas)。

    与 skeletonFile() 配套; 任一改变都会在 50ms 后触发(重新)加载。
    \sa skeletonFile(), setAtlasFile()

*/
QString SpineWidget::atlasFile() const
{
    return d->atlasFile;
}

/*!
    \since Visindigo 0.16.2
    设置图集文件路径并触发延迟加载(50ms)。

    \a path .atlas 文件路径(相对或绝对)
    \sa setSkeletonFile(), reload()

*/
void SpineWidget::setAtlasFile(const QString& path)
{
    if (d->atlasFile == path)
        return;
    d->atlasFile = path;
    emit atlasFileChanged(d->atlasFile);
    d->lazyLoadTimer->stop();
    d->lazyLoadTimer->start();
}

/*!
    \property SpineWidget::skeletonFile
    \since Visindigo 0.16.2
    当前骨骼数据文件路径(.skel 或 .json)。

    \sa atlasFile(), setSkeletonFile()

*/
QString SpineWidget::skeletonFile() const
{
    return d->skeletonFile;
}

/*!
    \since Visindigo 0.16.2
    设置骨骼数据文件路径并触发延迟加载(50ms)。

    \a path .skel/.json 文件路径
    \sa setAtlasFile(), reload()

*/
void SpineWidget::setSkeletonFile(const QString& path)
{
    if (d->skeletonFile == path)
        return;
    d->skeletonFile = path;
    emit skeletonFileChanged(d->skeletonFile);
    d->lazyLoadTimer->stop();
    d->lazyLoadTimer->start();
}

/*!
    \since Visindigo 0.16.2
    重新加载当前 atlas + skeleton 资源。

    重启内部 50ms 懒加载定时器, 之后在后台线程重新解析并重建骨架。
    适合在资源文件被替换后强制刷新。

    \sa setAtlasFile(), setSkeletonFile(), resourceReady()

*/
void SpineWidget::reload()
{
    d->lazyLoadTimer->stop();
    d->lazyLoadTimer->start();
}

/*!
    \property SpineWidget::loaded
    \since Visindigo 0.16.2
    资源文件是否已解析完成。

    注意: 解析完成不代表骨架可立即渲染(见 isSkeletonReady())。
    加载成功/失败分别发射 resourceReady() / resourceLoadFailed()。

    \sa isSkeletonReady(), resourceReady()

*/
bool SpineWidget::loaded() const
{
    return d->loaded;
}

/*!
    \property SpineWidget::isSkeletonReady
    \since Visindigo 0.16.2
    骨架是否已就绪可渲染。

    为 true 表示 atlas/skeleton/animationState 均已创建且 worker 线程在运行,
    此时才可安全设置动画/皮肤等。

    \sa loaded(), resourceReady()

*/
bool SpineWidget::isSkeletonReady() const
{
    return d->loaded && d->atlas && d->skeleton && d->animationState && d->workerThread->isRunning();
}

/*!
    \since Visindigo 0.16.2
    将整个骨架恢复到 Setup Pose(初始姿态)。

    投递到 worker 线程执行(线程安全)。
    \sa setBonesToSetupPose(), setSlotsToSetupPose()

*/
void SpineWidget::setToSetupPose()
{
    if (d->workerThread->isRunning() && d->worker)
        QMetaObject::invokeMethod(d->worker, "setToSetupPose");
}

/*!
    \since Visindigo 0.16.2
    仅将骨骼恢复到 Setup Pose(保持插槽/附件现状)。

    \sa setToSetupPose(), setSlotsToSetupPose()

*/
void SpineWidget::setBonesToSetupPose()
{
    if (d->workerThread->isRunning() && d->worker)
        QMetaObject::invokeMethod(d->worker, "setBonesToSetupPose");
}

/*!
    \since Visindigo 0.16.2
    仅将插槽恢复到 Setup Pose(附件/颜色等)。

    \sa setToSetupPose(), setBonesToSetupPose()

*/
void SpineWidget::setSlotsToSetupPose()
{
    if (d->workerThread->isRunning() && d->worker)
        QMetaObject::invokeMethod(d->worker, "setSlotsToSetupPose");
}

/*!
    \since Visindigo 0.16.2
    替换指定插槽(slot)上的附件(attachment)。

    \a slotName 插槽名称
    \a attachmentName 附件名称; 传空字符串可移除该插槽的附件
    \return 恒为 true(异步投递, 实际结果由 worker 线程处理)
    \sa setSkin()

*/
bool SpineWidget::setAttachment(const QString& slotName, const QString& attachmentName)
{
    if (d->workerThread->isRunning() && d->worker)
        QMetaObject::invokeMethod(d->worker, "setAttachment", Q_ARG(QString, slotName), Q_ARG(QString, attachmentName));
    return true;
}

/*!
    \since Visindigo 0.16.2
    设置两个动画之间切换的混合时长(仅对这两个动画生效)。

    \a fromAnimation 源动画名
    \a toAnimation 目标动画名
    \a duration 混合时长(秒); 0 表示立即切换
    \sa setDefaultMix()

*/
void SpineWidget::setMix(const QString& fromAnimation, const QString& toAnimation, float duration)
{
    if (d->workerThread->isRunning() && d->worker)
        QMetaObject::invokeMethod(d->worker, "setMix",
                                  Q_ARG(QString, fromAnimation), Q_ARG(QString, toAnimation), Q_ARG(float, duration));
}

/*!
    \since Visindigo 0.16.2
    在当前轨道(track)上设置动画并立即播放。

    会替换该轨道上正在播放的动画, 并启动内部动画驱动定时器。
    轨道 0 通常用于主循环动画; 更高轨道用于叠加(如表情), 切换时按
    setMix()/setDefaultMix() 混合。

    \a trackIndex 轨道索引(>=0)
    \a name 动画名(须存在于 animations())
    \a loop 是否循环播放
    \sa addAnimation(), clearTrack(), animations(), animationStarted()

*/
void SpineWidget::setAnimation(qint32 trackIndex, const QString& name, bool loop)
{
    if (d->workerThread->isRunning() && d->worker)
        QMetaObject::invokeMethod(d->worker, "setAnimation", Q_ARG(qint32, trackIndex), Q_ARG(QString, name), Q_ARG(bool, loop));
    startAnimationDriver();   // 驱动动画播放(固定帧率走 animTimer; 自适应走 worker 自驱动)
}

/*!
    \since Visindigo 0.16.2
    在当前轨道末尾排队一个动画(当前动画播完后再播)。

    \a trackIndex 轨道索引
    \a name 动画名
    \a loop 是否循环
    \a delay 延迟秒数; 0 表示紧接当前动画之后播放
    \sa setAnimation(), clearTrack()

*/
void SpineWidget::addAnimation(qint32 trackIndex, const QString& name, bool loop, float delay)
{
    if (d->workerThread->isRunning() && d->worker)
        QMetaObject::invokeMethod(d->worker, "addAnimation",
                                  Q_ARG(qint32, trackIndex), Q_ARG(QString, name), Q_ARG(bool, loop), Q_ARG(float, delay));
    startAnimationDriver();   // 驱动动画播放(固定帧率走 animTimer; 自适应走 worker 自驱动)
}

/*!
    \since Visindigo 0.16.2
    设置皮肤(skin)。

    \a skinName 皮肤名(须存在于 skins()); 传默认皮肤名可切回默认皮肤
    \sa skins()

*/
void SpineWidget::setSkin(const QString& skinName)
{
    if (d->workerThread->isRunning() && d->worker)
        QMetaObject::invokeMethod(d->worker, "setSkin", Q_ARG(QString, skinName));
}

/*!
    \since Visindigo 0.16.2
    清空所有轨道上的动画(停止播放, 骨架保留当前姿态)。

    \sa clearTrack(), setAnimation()

*/
void SpineWidget::clearTracks()
{
    if (d->workerThread->isRunning() && d->worker)
        QMetaObject::invokeMethod(d->worker, "clearTracks");
}

/*!
    \since Visindigo 0.16.2
    清空指定轨道上的动画。

    \a trackIndex 轨道索引(>=0)
    \sa clearTracks(), setAnimation()

*/
void SpineWidget::clearTrack(qint32 trackIndex)
{
    if (d->workerThread->isRunning() && d->worker)
        QMetaObject::invokeMethod(d->worker, "clearTrack", Q_ARG(qint32, trackIndex));
}

/*!
    \property SpineWidget::animations
    \since Visindigo 0.16.2
    资源中可用的动画名列表(加载完成后可用)。

    列表按轨道优先级从高到低排列: 第一个元素为最高优先级(叠加在最上层)的动画,
    最后一个元素为最低优先级(track 0, 通常用作常驻基础动画)。UI 可据此从上到下
    排列, 并将第 i 个动画分配到轨道 size-1-i。

    \sa skins(), setAnimation(), resourceReady()

*/
QStringList SpineWidget::animations() const
{
    return d->animations;
}

/*!
    \property SpineWidget::skins
    \since Visindigo 0.16.2
    资源中可用的皮肤名列表(加载完成后可用)。

    \sa setSkin(), resourceReady()

*/
QStringList SpineWidget::skins() const
{
    return d->skins;
}

/*!
    \since Visindigo 0.16.2
    当前应用的皮肤名。

    加载完成后为 loadResource 自动选择的初始皮肤(默认取附件最多的皮肤);
    之后随 setSkin() 同步更新。
    \sa skins(), setSkin()

*/
QString SpineWidget::currentSkin() const
{
    return d->currentSkin;
}

/*!
    \since Visindigo 0.16.2
    当前 GL 渲染器字符串(如 "NVIDIA GeForce RTX 3060 Laptop GPU" / "Intel(R) UHD Graphics")。

    用于多 GPU 笔记本诊断: 确认进程实际跑在集显还是独显。GL 上下文首次初始化
    (initializeGL)后才有值, 此前返回空字符串。
    \sa gpuVendor()

*/
QString SpineWidget::gpuRenderer() const
{
    return d->gpuRenderer;
}

/*!
    \since Visindigo 0.16.2
    当前 GL 厂商字符串(如 "NVIDIA Corporation" / "Intel")。

    与 gpuRenderer() 配合用于确认实际使用的 GPU。
    \sa gpuRenderer()

*/
QString SpineWidget::gpuVendor() const
{
    return d->gpuVendor;
}

/*!
    \property SpineWidget::sourceSize
    \since Visindigo 0.16.2
    资源源尺寸(px)。

    一般来自图集或骨架生成信息, 可用于布局参考。
    \sa preferSize()

*/
QSize SpineWidget::sourceSize() const
{
    return d->sourceSize;
}

/*!
    \since Visindigo 0.16.2
    内容在自然宽高比(不变形)下完整显示时理应的尺寸。

    返回世界坐标宽高(QPointF::x()=宽, y()=高), 不依赖外部 widget 大小。
    渲染采用 cover 等比适配: widget 比例与内容不一致时内容溢出裁剪而非拉伸变形。
    可作为外部布局设置窗口尺寸的依据。

    \return 内容自然尺寸(世界坐标)
    \sa sourceSize(), preferSizeChanged()

*/
QPointF SpineWidget::preferSize() const
{
    // 内容在自然宽高比下完整显示时理应的尺寸(世界坐标宽高), 不依赖外部 widget 大小。
    const QRectF r = d->hasViewport ? d->viewportRect
                                    : (d->renderRect.isEmpty() ? d->boundingRect : d->renderRect);
    return QPointF(r.width(), r.height());
}

/*!
    \since Visindigo 0.16.2
    世界原点 (0,0) 在控件中的偏移(百分比)。

    x/y 不再限制在 [-1,1], 右/下为正: +1=贴右/底边, 0=中心, -1=贴左/顶边。
    默认 (0,0) 即原点居中。常用于把角色偏置(例如只显示上半身)。

    \sa setAnchorOffset()

*/
QPointF SpineWidget::anchorOffset() const
{
    return d->anchorOffset;
}

/*!
    \since Visindigo 0.16.2
    设置原点偏移(见 \l anchorOffset 说明)。

    \a offset 百分比偏移 QPointF(x, y); 已解除 [-1,1] 限制(可被交互拖动/滑块设为任意值)
    \sa anchorOffset()

*/
void SpineWidget::setAnchorOffset(const QPointF& offset)
{
    if (d->anchorOffset == offset)
        return;
    d->anchorOffset = offset;
    emit anchorOffsetChanged(d->anchorOffset);
    update();
}

/*!
    \since Visindigo 0.16.2
    是否开启鼠标交互(滚轮缩放 / 左键拖动平移), 默认开启。

    \sa setInteractEnable()

*/
bool SpineWidget::isInteractEnable() const { return d->interactEnable; }
void SpineWidget::setInteractEnable(bool enable) { d->interactEnable = enable; }

/*!
    \since Visindigo 0.16.2
    鼠标滚轮 = 缩放(以当前 skeletonScale 为基准, 上滚 ×1.1 / 下滚 ×0.9, 限 0.1~20)。

*/
void SpineWidget::wheelEvent(QWheelEvent* event)
{
    if (not d->interactEnable || not isSkeletonReady()) {
        QOpenGLWidget::wheelEvent(event);
        return;
    }
    const int delta = event->angleDelta().y();
    qreal factor = 1.0;
    if (delta > 0)
        factor = 1.1;          // 上滚放大
    else if (delta < 0)
        factor = 0.9;          // 下滚缩小
    else {
        QOpenGLWidget::wheelEvent(event);
        return;
    }
    const qreal vw = qMax<qreal>(1.0, width());
    const qreal vh = qMax<qreal>(1.0, height());
    // 缩放锚点 = 鼠标所在位置: 缩放前记录鼠标下的世界坐标, 缩放后再调整 anchorOffset
    // 使其回到鼠标处, 避免内容偏向一边。
    const QPointF pos = event->position();
    const float ndcX = float(2.0 * pos.x() / vw - 1.0);
    const float ndcY = float(1.0 - 2.0 * pos.y() / vh);
    const QVector3D world = d->projectionMatrix().inverted().map(QVector3D(ndcX, ndcY, 0.0f));

    d->skeletonScale = qBound<qreal>(0.1, d->skeletonScale * factor, 20.0);
    emit skeletonScaleChanged(d->skeletonScale);

    // 缩放后该世界点的屏幕位置与鼠标位置之差, 换算成 anchorOffset 修正量
    const QVector3D ndcAfter = d->projectionMatrix().map(world);
    const qreal pxAfter = (ndcAfter.x() + 1.0f) * 0.5f * vw;
    const qreal pyAfter = (1.0f - ndcAfter.y()) * 0.5f * vh;
    d->anchorOffset.rx() += 2.0 * (pos.x() - pxAfter) / vw;
    d->anchorOffset.ry() += 2.0 * (pos.y() - pyAfter) / vh;
    emit anchorOffsetChanged(d->anchorOffset);

    update();
    event->accept();
}

/*!
    \since Visindigo 0.16.2
    左键按下: 开始拖动(记录起始位置)。

*/
void SpineWidget::mousePressEvent(QMouseEvent* event)
{
    if (d->interactEnable && event->button() == Qt::LeftButton) {
        d->dragging = true;
        d->lastDragPos = event->position();
        event->accept();
        return;
    }
    QOpenGLWidget::mousePressEvent(event);
}

/*!
    \since Visindigo 0.16.2
    左键拖动: 更新 anchorOffset 实现平移(控件像素 -> 归一化偏移)。

*/
void SpineWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (d->interactEnable && d->dragging) {
        const QPointF pos = event->position();
        const QPointF delta = pos - d->lastDragPos;
        d->lastDragPos = pos;
        d->anchorOffset.rx() += 2.0 * delta.x() / qMax<qreal>(1.0, width());
        d->anchorOffset.ry() += 2.0 * delta.y() / qMax<qreal>(1.0, height());
        emit anchorOffsetChanged(d->anchorOffset);
        update();
        event->accept();
        return;
    }
    QOpenGLWidget::mouseMoveEvent(event);
}

/*!
    \since Visindigo 0.16.2
    左键释放: 结束拖动。

*/
void SpineWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (d->interactEnable && event->button() == Qt::LeftButton) {
        d->dragging = false;
        event->accept();
        return;
    }
    QOpenGLWidget::mouseReleaseEvent(event);
}

/*!
    \since Visindigo 0.16.2
    是否开启点击穿透。

    开启后本控件在命中测试时, 对透明像素(空白, 未画骨架)返回 HTTRANSPARENT,
    鼠标点击穿透到下层窗口, 常用于透明/无边框场景; 骨架本体仍可交互
    (透明度判定见 hitTest())。注意: HTTRANSPARENT 只对同线程下层窗口生效;
    跨进程/到桌面的穿透需在顶层窗口配合 WS_EX_TRANSPARENT(见 DemoWindow::startClickThrough)。

    \sa setClickThrough()

*/
bool SpineWidget::clickThrough() const { return d->clickThrough; }

/*!
    \since Visindigo 0.16.2
    设置点击穿透开关(见 \l clickThrough 说明)。

    \a clickThrough true=命中测试穿透, false=正常
    \sa clickThrough()

*/
void SpineWidget::setClickThrough(bool clickThrough)
{
    if (d->clickThrough == clickThrough)
        return;
    d->clickThrough = clickThrough;
}

/*!
    \since Visindigo 0.16.2
    命中测试: 判断 \a widgetPos(控件本地坐标, 左上角原点, Y 向下)处是否画着骨架。

    返回 true 表示该像素"不透明"(骨架覆盖), false 表示透明(空白)。用于透明穿透场景:
    透明处点击应穿透到下层窗口/桌面, 骨架本体应保持可交互。

    实现: 无锁 CPU 几何命中——将控件坐标经渲染同款投影矩阵的逆变换到世界坐标,
    对渲染批次(renderSnapshot)做点在三角形内测试, 命中后用重心坐标插值 UV,
    采样图集贴图 alpha 判定透明度; 不触发任何 GPU 回读, 与渲染线程零竞争。

    \return true=骨架覆盖(不透明), false=透明(空白); 未就绪/无内容时返回 false
    \sa setClickThrough()

*/
bool SpineWidget::hitTest(const QPoint& widgetPos) const
{
    return d->hitTest(widgetPos);
}

/*!
    \since Visindigo 0.16.2
    Windows 原生事件处理。

    \reimp
    \note 点击穿透开启时, 仅当鼠标落在本控件的透明像素(空白)上才对 WM_NCHITTEST
    返回 HTTRANSPARENT; 落在骨架本体上保持可交互。HTTRANSPARENT 只对同线程下层窗口
    生效; 跨进程/到桌面的穿透需配合顶层窗口的 WS_EX_TRANSPARENT
    (见 DemoWindow::startClickThrough)。

*/
bool SpineWidget::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
    Q_UNUSED(eventType)
    MSG* msg = static_cast<MSG*>(message);
    // 开启点击穿透时, 鼠标在本控件上的命中测试返回 HTTRANSPARENT。
    // 注意: HTTRANSPARENT 只对同线程下层窗口生效; 跨进程/到桌面的穿透需配合
    // 顶层窗口的 WS_EX_TRANSPARENT(见 DemoWindow::startClickThrough)。
    if (msg->message == WM_NCHITTEST && d->clickThrough) {
        // 用 QCursor::pos()(Qt 逻辑坐标, DPI-aware) 而非 WM_NCHITTEST 的物理像素 lParam:
        // 高 DPI 下物理像素与 Qt 逻辑坐标相差 devicePixelRatio 倍, 直接混用会使命中区域
        // 偏移(表现为只有左上角能穿透)。mapFromGlobal/rect/hitTest 全部使用逻辑坐标。
        const QPoint local = mapFromGlobal(QCursor::pos());
        // 仅当鼠标落在透明像素(空白/未画骨架)上才返回 HTTRANSPARENT, 骨架本体保持可交互
        if (rect().contains(local) && not hitTest(local)) {
            *result = HTTRANSPARENT;
            return true;
        }
    }
#else
    Q_UNUSED(eventType) Q_UNUSED(message) Q_UNUSED(result)
#endif
    return QOpenGLWidget::nativeEvent(eventType, message, result);
}

/*!
    \since Visindigo 0.16.2
    控件变为可见时回调。

    自适应帧率模式下 worker 以"算完立即再算"自驱动; 控件最小化/隐藏期间
    worker 因不可见会提前返回并停止自驱动循环(见 updateSkeletonAnimation),
    此处恢复可见时重新 kick 循环, 避免动画与 AutoFPS 永久暂停。

    \reimp
*/
void SpineWidget::showEvent(QShowEvent* event)
{
    QOpenGLWidget::showEvent(event);
    if (d->autoFpsEnable && d->workerThread->isRunning() && d->worker)
        QMetaObject::invokeMethod(d->worker, "kickAutoTick");
}

/*!
    \property SpineWidget::skeletonScale
    \since Visindigo 0.16.2
    骨架整体缩放倍率, 默认 1.0。

    作用于 scaleX/scaleY 之前。缩放通过投影矩阵围绕锚点进行, 不会改变骨架自身。
    \sa setSkeletonScale(), setScaleX(), setScaleY()

*/
qreal SpineWidget::skeletonScale() const { return d->skeletonScale; }

/*!
    \since Visindigo 0.16.2
    设置骨架整体缩放倍率。

    \a value 缩放倍率(>0)
    \sa skeletonScale()

*/
void SpineWidget::setSkeletonScale(const qreal& value)
{
    d->skeletonScale = value;
    if (isSkeletonReady())
        QMetaObject::invokeMethod(d->worker, "applyScale");
    emit skeletonScaleChanged(d->skeletonScale);
}

/*!
    \property SpineWidget::timeScale
    \since Visindigo 0.16.2
    动画时间倍率(播放速度), 默认 1.0。

    2.0=两倍速, 0.5=半速, 0=暂停。
    \sa setTimeScale()

*/
qreal SpineWidget::timeScale() const { return d->timeScale; }

/*!
    \since Visindigo 0.16.2
    设置动画播放速度倍率。

    \a value 时间倍率
    \sa timeScale()

*/
void SpineWidget::setTimeScale(const qreal& value)
{
    d->timeScale = value;
    emit timeScaleChanged(d->timeScale);
}

/*!
    \property SpineWidget::fps
    \since Visindigo 0.16.2
    动画刷新帧率(每秒更新骨架次数), 默认 25。
    \sa setFps()

*/
qint32 SpineWidget::fps() const { return d->fps; }

/*!
    \since Visindigo 0.16.2
    设置动画刷新帧率。

    动画定时器间隔按 qMax(1, fps) 计算, 避免非正值产生非法定时器间隔。

    \a fps 帧率(建议 >=1)
    \sa fps()

*/
void SpineWidget::setFps(qint32 fps)
{
    d->fps = fps;
    if (d->animTimer && not d->autoFpsEnable)
        d->animTimer->setInterval(1000 / qMax(1, d->fps));
    emit fpsChanged(d->fps);
}

/*
    \brief 启动动画驱动(内部): 固定帧率走 animTimer; 自适应模式走 worker 自驱动 kick。
*/
void SpineWidget::startAnimationDriver()
{
    if (d->autoFpsEnable) {
        if (d->workerThread->isRunning() && d->worker)
            QMetaObject::invokeMethod(d->worker, "kickAutoTick");
    } else {
        d->animTimer->start();
    }
}

/*!
    \property SpineWidget::autoFpsEnable
    \since Visindigo 0.16.2
    自适应帧率开关, 默认关闭。

    开启后不再按 setFps() 固定限帧, 内部改为"算完一帧立即算下一帧"的
    自驱动模式, 帧率完全由计算+渲染耗时决定, 能跑多快跑多快(上限受显示器
    刷新率/vsync 限制)。关闭后恢复 setFps() 固定帧率。当前实际帧率可通过
    getAutoFps() 查询。
    \sa setAutoFpsEnable(), getAutoFps(), setFps()

*/
bool SpineWidget::isAutoFpsEnable() const
{
    return d->autoFpsEnable;
}

/*!
    \since Visindigo 0.16.2
    设置自适应帧率开关(见 \l autoFpsEnable 说明)。

    \a enable true=能跑多快跑多快, false=恢复 setFps() 固定帧率
    \sa isAutoFpsEnable(), getAutoFps()

*/
void SpineWidget::setAutoFpsEnable(bool enable)
{
    if (d->autoFpsEnable == enable)
        return;
    d->autoFpsEnable = enable;
    d->autoFps = 0;
    if (enable) {
        // 自适应: 停用固定间隔定时器, 由 worker "算完立即再算" 自驱动, 能跑多快跑多快
        if (d->animTimer)
            d->animTimer->stop();
        if (d->workerThread->isRunning() && d->worker)
            QMetaObject::invokeMethod(d->worker, "kickAutoTick");
    } else {
        // 恢复固定帧率
        if (d->animTimer) {
            d->animTimer->setInterval(1000 / qMax(1, d->fps));
            d->animTimer->start();
        }
    }
    emit autoFpsEnableChanged(d->autoFpsEnable);
}

/*!
    \since Visindigo 0.16.2
    当前实际帧率(自适应模式开启并已测量后有效)。

    返回最近平滑后的实际渲染帧率; 未开启或尚未测得时返回 0。
    \sa setAutoFpsEnable(), autoFpsEnable()

*/
double SpineWidget::getAutoFps() const
{
    return d->autoFps;
}

/*!
    \property SpineWidget::defaultMix
    \since Visindigo 0.16.2
    默认动画混合时长(秒), 用于未单独 setMix() 的动画切换, 默认 0.1。
    \sa setDefaultMix(), setMix()

*/
qreal SpineWidget::defaultMix() const { return d->defaultMix; }

/*!
    \since Visindigo 0.16.2
    设置默认动画混合时长, 并同步到 Spine 的 AnimationStateData。

    \a value 混合时长(秒)
    \sa defaultMix(), setMix()

*/
void SpineWidget::setDefaultMix(const qreal& value)
{
    d->defaultMix = value;
    // 必须同步到 Spine 的 AnimationStateData, 否则运行时拖 Mix slider 不生效,
    // 切换动画仍用加载时的默认混合时长(表现为点击后反应慢/有过渡)。
    if (d->animationStateData)
        d->animationStateData->setDefaultMix(value);
    emit defaultMixChanged(d->defaultMix);
}

/*!
    \property SpineWidget::scaleX
    \since Visindigo 0.16.2
    X 轴缩放, 1=原大小, 负值水平翻转(围绕锚点翻转, 不会移出窗口)。
    \sa setScaleX(), setScaleY(), setSkeletonScale()

*/
qreal SpineWidget::scaleX() const { return d->scaleX; }

/*!
    \since Visindigo 0.16.2
    设置 X 轴缩放(见 \l scaleX 说明)。

    \a value 缩放值(可为负实现水平翻转)
    \sa scaleX()

*/
void SpineWidget::setScaleX(const qreal& value)
{
    d->scaleX = value;
    if (isSkeletonReady())
        QMetaObject::invokeMethod(d->worker, "applyScale");
    emit scaleXChanged(d->scaleX);
}

/*!
    \property SpineWidget::scaleY
    \since Visindigo 0.16.2
    Y 轴缩放, 1=原大小, 负值垂直翻转(围绕锚点翻转, 不会移出窗口)。
    \sa setScaleY(), setScaleX(), setSkeletonScale()

*/
qreal SpineWidget::scaleY() const { return d->scaleY; }

/*!
    \since Visindigo 0.16.2
    设置 Y 轴缩放(见 \l scaleY 说明)。

    \a value 缩放值(可为负实现垂直翻转)
    \sa scaleY()

*/
void SpineWidget::setScaleY(const qreal& value)
{
    d->scaleY = value;
    if (isSkeletonReady())
        QMetaObject::invokeMethod(d->worker, "applyScale");
    emit scaleYChanged(d->scaleY);
}

/*!
    \property SpineWidget::blendColor
    \since Visindigo 0.16.2
    调色混合颜色(对应着色器 u_blendColor), 默认白色(不改变原色)。
    \sa setBlendColor()

*/
QColor SpineWidget::blendColor() const { return d->blendColor; }

/*!
    \since Visindigo 0.16.2
    设置调色混合颜色。

    \a color 混合色
    \sa blendColor()

*/
void SpineWidget::setBlendColor(const QColor& color)
{
    d->blendColor = color;
    emit blendColorChanged(d->blendColor);
}

/*!
    \property SpineWidget::blendColorChannel
    \since Visindigo 0.16.2
    调色混合通道索引(对应着色器 u_blendColorChannel), -1 表示不启用。
    \sa setBlendColorChannel()

*/
qint32 SpineWidget::blendColorChannel() const { return d->blendColorChannel; }

/*!
    \since Visindigo 0.16.2
    设置调色混合通道。

    \a channel 通道索引
    \sa blendColorChannel()

*/
void SpineWidget::setBlendColorChannel(qint32 channel)
{
    d->blendColorChannel = channel;
    emit blendColorChannelChanged(d->blendColorChannel);
}

/*!
    \property SpineWidget::light
    \since Visindigo 0.16.2
    亮度系数(对应着色器 u_light), 默认 1.0。
    \sa setLight()

*/
float SpineWidget::light() const { return d->light; }

/*!
    \since Visindigo 0.16.2
    设置亮度系数。

    \a light 亮度(0 及以上)
    \sa light()

*/
void SpineWidget::setLight(float light)
{
    d->light = light;
    emit lightChanged(d->light);
}

/*!
    \property SpineWidget::debugBones
    \since Visindigo 0.16.2
    是否绘制骨骼调试线。
    \sa setDebugBones()

*/
bool SpineWidget::debugBones() const { return d->debugBones; }

/*!
    \since Visindigo 0.16.2
    开启/关闭骨骼调试线绘制。

    \a debugBones true=显示
    \sa debugBones(), debugSlots(), debugMesh()

*/
void SpineWidget::setDebugBones(bool debugBones)
{
    d->debugBones = debugBones;
    emit debugBonesChanged(d->debugBones);
    update();
}

/*!
    \property SpineWidget::debugSlots
    \since Visindigo 0.16.2
    是否绘制插槽(附件包围盒)调试线。
    \sa setDebugSlots()

*/
bool SpineWidget::debugSlots() const { return d->debugSlots; }

/*!
    \since Visindigo 0.16.2
    开启/关闭插槽调试线绘制。

    \a debugSlots true=显示
    \sa debugSlots(), debugBones(), debugMesh()

*/
void SpineWidget::setDebugSlots(bool debugSlots)
{
    d->debugSlots = debugSlots;
    emit debugSlotsChanged(d->debugSlots);
    update();
}

/*!
    \property SpineWidget::debugMesh
    \since Visindigo 0.16.2
    是否绘制网格(mesh)调试线。
    \sa setDebugMesh()

*/
bool SpineWidget::debugMesh() const { return d->debugMesh; }

/*!
    \since Visindigo 0.16.2
    开启/关闭网格调试线绘制。

    \a debugMesh true=显示
    \sa debugMesh(), debugBones(), debugSlots()

*/
void SpineWidget::setDebugMesh(bool debugMesh)
{
    d->debugMesh = debugMesh;
    emit debugMeshChanged(d->debugMesh);
    update();
}

/*!
    \property SpineWidget::debugBonesWidth
    \since Visindigo 0.16.2
    骨骼调试线宽(默认 2)。
*/
qreal SpineWidget::debugBonesWidth() const { return d->debugBonesWidth; }
void SpineWidget::setDebugBonesWidth(qreal width) { d->debugBonesWidth = width; emit debugStyleChanged(); update(); }

/*!
    \property SpineWidget::debugSlotsWidth
    \since Visindigo 0.16.2
    插槽调试线宽(默认 1)。
*/
qreal SpineWidget::debugSlotsWidth() const { return d->debugSlotsWidth; }
void SpineWidget::setDebugSlotsWidth(qreal width) { d->debugSlotsWidth = width; emit debugStyleChanged(); update(); }

/*!
    \property SpineWidget::debugMeshWidth
    \since Visindigo 0.16.2
    网格调试线宽(默认 1)。
*/
qreal SpineWidget::debugMeshWidth() const { return d->debugMeshWidth; }
void SpineWidget::setDebugMeshWidth(qreal width) { d->debugMeshWidth = width; emit debugStyleChanged(); update(); }

/*!
    \since Visindigo 0.16.2
    统一设置三种调试线条(骨骼/插槽/网格)的宽度, UI 滑块用。
*/
void SpineWidget::setDebugLineWidth(qreal width)
{
    d->debugBonesWidth = width;
    d->debugSlotsWidth = width;
    d->debugMeshWidth = width;
    emit debugStyleChanged();
    update();
}

/*!
    \property SpineWidget::debugBonesColor
    \since Visindigo 0.16.2
    骨骼调试线颜色(默认 QColor(200,150,40,255))。
*/
QColor SpineWidget::debugBonesColor() const { return d->debugBonesColor; }
void SpineWidget::setDebugBonesColor(const QColor& color) { d->debugBonesColor = color; emit debugStyleChanged(); update(); }

/*!
    \property SpineWidget::debugSlotsColor
    \since Visindigo 0.16.2
    插槽调试线颜色(默认 QColor(200,40,150,255))。
*/
QColor SpineWidget::debugSlotsColor() const { return d->debugSlotsColor; }
void SpineWidget::setDebugSlotsColor(const QColor& color) { d->debugSlotsColor = color; emit debugStyleChanged(); update(); }

/*!
    \property SpineWidget::debugMeshColor
    \since Visindigo 0.16.2
    网格调试线颜色(默认 QColor(40,150,200,255))。
*/
QColor SpineWidget::debugMeshColor() const { return d->debugMeshColor; }
void SpineWidget::setDebugMeshColor(const QColor& color) { d->debugMeshColor = color; emit debugStyleChanged(); update(); }

/*!
    \property SpineWidget::backgroundColor
    \since Visindigo 0.16.2
    控件背景色, 默认透明 QColor(0,0,0,0)。

    \sa setBackgroundColor()

*/
QColor SpineWidget::backgroundColor() const { return d->backgroundColor; }

/*!
    \since Visindigo 0.16.2
    设置背景色。

    \a color 背景色(带 alpha); 透明背景需配合顶层窗口的 WA_TranslucentBackground
    \sa backgroundColor()

*/
void SpineWidget::setBackgroundColor(const QColor& color)
{
    d->backgroundColor = color;
    emit backgroundColorChanged(d->backgroundColor);
    update();
}

/*!
    \since Visindigo 0.16.2
    GL 上下文初始化: 初始化 GL 函数、编译着色器、注入 GL 接口到私有实现。

    \reimp
    \note 上下文可能被重建(QOpenGLWidget 窗口重建), 此时旧纹理会被失效并在下次使用时重建。

*/
void SpineWidget::initializeGL()
{
    initializeOpenGLFunctions();
    d->gl = this;   // 注入 GL 接口供 SpineWidgetPrivate 的渲染方法使用
    d->initShaders();
    d->glReady = true;
    // 记录当前实际使用的 GPU(多显卡笔记本诊断: 确认跑在集显还是独显);
    // GL 上下文可能被重建, 每次重建都重新读取。
    if (const GLubyte* vendor = glGetString(GL_VENDOR))
        d->gpuVendor = QString::fromLatin1(reinterpret_cast<const char*>(vendor));
    if (const GLubyte* renderer = glGetString(GL_RENDERER))
        d->gpuRenderer = QString::fromLatin1(reinterpret_cast<const char*>(renderer));
    // 上下文可能被重建(QOpenGLWidget 窗口重建), 使旧的 GL 纹理失效
    AimyTextureLoader::instance()->invalidateGlTextures();
    update();
}

/*!
    \since Visindigo 0.16.2
    GL 视口尺寸变化时的回调。

    \reimp
    \note 当前无需特殊处理: 投影在 paintGL() 中按控件当前尺寸实时计算。

*/
void SpineWidget::resizeGL(qint32 w, qint32 h)
{
    Q_UNUSED(w)
    Q_UNUSED(h)
}

/*!
    \since Visindigo 0.16.2
    绘制一帧: 用背景色清屏, 然后渲染骨架批次(纹理/调试线)。

    \reimp
    \sa renderBatches()

*/
void SpineWidget::paintGL()
{
    glClearColor(d->backgroundColor.redF(), d->backgroundColor.greenF(),
                 d->backgroundColor.blueF(), d->backgroundColor.alphaF());
    glClear(GL_COLOR_BUFFER_BIT);

    if (not d->glReady || not d->loaded || not d->skeleton)
        return;
    d->renderBatches();
}

} // namespace Visindigo::Widgets
