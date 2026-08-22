#include "demo.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QTimer>
#include <QSlider>
#include <QScrollArea>
#include <QPalette>
#include <QDebug>
#include <QCursor>
#ifdef Q_OS_WIN
#  include <windows.h>
#endif
#include "spinewidget.h"

DemoWindow::DemoWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("VISpineWidget Demo - QtWidgets"));
    resize(1280, 800);

    // 整窗透明: QOpenGLWidget 的 alpha 需要顶层窗口也设 WA_TranslucentBackground,
    // 否则 GL 透明区域会被 Qt 用黑色填充 (而不是透出背景/桌面)
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);

    // ---- 左侧: Spine 渲染区 ----
    m_spine = new Visindigo::Widgets::SpineWidget(this);
    m_spine->setMinimumSize(480, 480);
    m_spine->setBackgroundColor(QColor(0, 0, 0, 0));   // 透明
    m_spine->setAtlasFile(QStringLiteral("examples/your_spine/your_spine.atlas"));
    m_spine->setSkeletonFile(QStringLiteral("examples/your_spine/your_spine.skel"));
    m_spine->setFps(60);
    m_spine->setDefaultMix(0.2);

    // ---- 右侧: 控制面板 (纯 QtWidgets) ----
    auto *rightPanel = new QWidget(this);
    rightPanel->setFixedWidth(400);
    // 窗体是透明的, 给右侧面板一个不透明背景以保证控件可读
    QPalette rp = rightPanel->palette();
    rp.setColor(QPalette::Window, QColor("#f0f2f5"));
    rightPanel->setPalette(rp);
    rightPanel->setAutoFillBackground(true);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setSpacing(10);

    // 动画
    auto *animBox = new QGroupBox(QStringLiteral("动画 Animation"), content);
    auto *animLayout = new QVBoxLayout(animBox);
    m_animGrid = new QGridLayout;
    m_animGrid->setSpacing(6);
    animLayout->addLayout(m_animGrid);
    layout->addWidget(animBox);

    // 皮肤
    auto *skinBox = new QGroupBox(QStringLiteral("皮肤 Skin"), content);
    auto *skinLayout = new QVBoxLayout(skinBox);
    m_skinGrid = new QGridLayout;
    m_skinGrid->setSpacing(6);
    skinLayout->addLayout(m_skinGrid);
    layout->addWidget(skinBox);

    // 调试
    auto *debugBox = new QGroupBox(QStringLiteral("调试 Debug"), content);
    auto *debugLayout = new QVBoxLayout(debugBox);
    m_debugBones = new QCheckBox(QStringLiteral("骨骼 Bones"), debugBox);
    m_debugSlots = new QCheckBox(QStringLiteral("插槽 Slots"), debugBox);
    m_debugMesh = new QCheckBox(QStringLiteral("网格 Mesh"), debugBox);
    connect(m_debugBones, &QCheckBox::toggled, this, [this](bool on) { m_spine->setDebugBones(on); });
    connect(m_debugSlots, &QCheckBox::toggled, this, [this](bool on) { m_spine->setDebugSlots(on); });
    connect(m_debugMesh, &QCheckBox::toggled, this, [this](bool on) { m_spine->setDebugMesh(on); });
    debugLayout->addWidget(m_debugBones);
    debugLayout->addWidget(m_debugSlots);
    debugLayout->addWidget(m_debugMesh);
    layout->addWidget(debugBox);

    // 属性
    auto *propBox = new QGroupBox(QStringLiteral("属性 Properties"), content);
    auto *propLayout = new QVBoxLayout(propBox);
    auto addPropRow = [&](const QString &name, QSlider *slider, QLabel *valLabel) {
        auto *row = new QHBoxLayout;
        auto *lbl = new QLabel(name, propBox);
        lbl->setFixedWidth(100);
        row->addWidget(lbl);
        row->addWidget(slider, 1);
        row->addWidget(valLabel);
        propLayout->addLayout(row);
    };

    QLabel *fpsVal = new QLabel(QStringLiteral("60"), propBox);
    QSlider *fpsSlider = makeSlider(1, 240, 60, &fpsVal);
    connect(fpsSlider, &QSlider::valueChanged, this, [this, fpsVal](int v) { m_spine->setFps(v); fpsVal->setText(QString::number(v)); });
    addPropRow(QStringLiteral("FPS"), fpsSlider, fpsVal);

    QLabel *scaleVal = new QLabel(QStringLiteral("1.00"), propBox);
    QSlider *scaleSlider = makeSlider(10, 200, 100, &scaleVal);
    connect(scaleSlider, &QSlider::valueChanged, this, [this, scaleVal](int v) { m_spine->setSkeletonScale(v / 100.0); scaleVal->setText(QString::number(v / 100.0, 'f', 2)); });
    addPropRow(QStringLiteral("缩放 Scale"), scaleSlider, scaleVal);

    QLabel *timeVal = new QLabel(QStringLiteral("1.00"), propBox);
    QSlider *timeSlider = makeSlider(10, 200, 100, &timeVal);
    connect(timeSlider, &QSlider::valueChanged, this, [this, timeVal](int v) { m_spine->setTimeScale(v / 100.0); timeVal->setText(QString::number(v / 100.0, 'f', 2)); });
    addPropRow(QStringLiteral("时间倍率"), timeSlider, timeVal);

    QLabel *xVal = new QLabel(QStringLiteral("1.00"), propBox);
    QSlider *xSlider = makeSlider(-200, 200, 100, &xVal);
    connect(xSlider, &QSlider::valueChanged, this, [this, xVal](int v) { m_spine->setScaleX(v / 100.0); xVal->setText(QString::number(v / 100.0, 'f', 2)); });
    addPropRow(QStringLiteral("X 缩放"), xSlider, xVal);

    QLabel *yVal = new QLabel(QStringLiteral("1.00"), propBox);
    QSlider *ySlider = makeSlider(-200, 200, 100, &yVal);
    connect(ySlider, &QSlider::valueChanged, this, [this, yVal](int v) { m_spine->setScaleY(v / 100.0); yVal->setText(QString::number(v / 100.0, 'f', 2)); });
    addPropRow(QStringLiteral("Y 缩放"), ySlider, yVal);

    QLabel *anchorXVal = new QLabel(QStringLiteral("0.00"), propBox);
    QSlider *anchorXSlider = makeSlider(-100, 100, 0, &anchorXVal);
    connect(anchorXSlider, &QSlider::valueChanged, this, [this, anchorXVal](int v) {
        m_spine->setAnchorOffset(QPointF(v / 100.0, m_spine->anchorOffset().y()));
        anchorXVal->setText(QString::number(v / 100.0, 'f', 2));
    });
    addPropRow(QStringLiteral("原点X偏移"), anchorXSlider, anchorXVal);

    QLabel *anchorYVal = new QLabel(QStringLiteral("0.00"), propBox);
    QSlider *anchorYSlider = makeSlider(-100, 100, 0, &anchorYVal);
    connect(anchorYSlider, &QSlider::valueChanged, this, [this, anchorYVal](int v) {
        m_spine->setAnchorOffset(QPointF(m_spine->anchorOffset().x(), v / 100.0));
        anchorYVal->setText(QString::number(v / 100.0, 'f', 2));
    });
    addPropRow(QStringLiteral("原点Y偏移"), anchorYSlider, anchorYVal);

    QLabel *mixVal = new QLabel(QStringLiteral("0.20"), propBox);
    QSlider *mixSlider = makeSlider(0, 200, 20, &mixVal);
    connect(mixSlider, &QSlider::valueChanged, this, [this, mixVal](int v) { m_spine->setDefaultMix(v / 100.0); mixVal->setText(QString::number(v / 100.0, 'f', 2)); });
    addPropRow(QStringLiteral("混合时长"), mixSlider, mixVal);

    auto *autoFpsCb = new QCheckBox(QStringLiteral("自适应帧率 Auto FPS"), propBox);
    connect(autoFpsCb, &QCheckBox::toggled, this, [this](bool on) { m_spine->setAutoFpsEnable(on); });
    propLayout->addWidget(autoFpsCb);
    layout->addWidget(propBox);

    // 背景
    auto *bgBox = new QGroupBox(QStringLiteral("背景 Background"), content);
    auto *bgLayout = new QHBoxLayout(bgBox);
    auto *transBtn = new QPushButton(QStringLiteral("透明"), bgBox);
    auto *darkBtn = new QPushButton(QStringLiteral("深色"), bgBox);
    connect(transBtn, &QPushButton::clicked, this, [this]() { m_spine->setBackgroundColor(QColor(0, 0, 0, 0)); });
    connect(darkBtn, &QPushButton::clicked, this, [this]() { m_spine->setBackgroundColor(QColor("#22343c")); });
    bgLayout->addWidget(transBtn);
    bgLayout->addWidget(darkBtn);
    layout->addWidget(bgBox);

    // 窗口
    auto *winBox = new QGroupBox(QStringLiteral("窗口 Window"), content);
    auto *winLayout = new QVBoxLayout(winBox);
    auto *framelessCb = new QCheckBox(QStringLiteral("无边框 Frameless"), winBox);
    connect(framelessCb, &QCheckBox::toggled, this, [this](bool on) {
        m_frameless = on;
        // 无边框时 Spine 区域点击穿透(透明区域 -> 桌面/下层窗口), 面板仍可点
        if (on)
            startClickThrough();
        else
            stopClickThrough();
        m_spine->setClickThrough(on);   // 库侧命中测试也置为穿透(双保险)
        // 无边框 <-> 有边框切换: hide/show 不能刷新 Qt 的 frame geometry(标题栏高度),
        // 导致鼠标坐标映射偏移(偏下)。用"最小化 -> 恢复"强制原生窗口重算 frame。
        const bool wasVisible = isVisible();
        setWindowFlag(Qt::FramelessWindowHint, on);
        if (wasVisible) {
            showMinimized();
            QTimer::singleShot(0, this, [this]() { showNormal(); });
        }
    });
    winLayout->addWidget(framelessCb);
    layout->addWidget(winBox);

    // 重置
    auto *resetBtn = new QPushButton(QStringLiteral("重置姿态 (Set to Setup Pose)"), content);
    connect(resetBtn, &QPushButton::clicked, this, [this]() {
        m_spine->clearTracks();
        m_spine->setToSetupPose();
    });
    layout->addWidget(resetBtn);

    // 状态
    m_statusLabel = new QLabel(QStringLiteral("加载中..."), content);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    layout->addStretch(1);
    scroll->setWidget(content);
    rightLayout->addWidget(scroll);

    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);
    mainLayout->addWidget(m_spine, 1);
    mainLayout->addWidget(rightPanel);

    connect(m_spine, &Visindigo::Widgets::SpineWidget::resourceReady, this, &DemoWindow::onResourceReady);
    connect(m_spine, &Visindigo::Widgets::SpineWidget::resourceLoadFailed, this, &DemoWindow::onLoadFailed);
    connect(m_spine, &Visindigo::Widgets::SpineWidget::resourceReady, this, &DemoWindow::updateStatus);
    connect(m_spine, &Visindigo::Widgets::SpineWidget::animationCompleted, this, &DemoWindow::updateStatus);
    
    m_statusTimer = new QTimer(this);
    m_statusTimer->setInterval(250);
    connect(m_statusTimer, &QTimer::timeout, this, &DemoWindow::updateStatus);
    m_statusTimer->start();
}

QSlider *DemoWindow::makeSlider(int from, int to, double value, QLabel **valueLabel)
{
    auto *slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(from, to);
    slider->setValue(static_cast<int>(value));
    slider->setFixedWidth(190);
    Q_UNUSED(valueLabel)
    return slider;
}

void DemoWindow::onResourceReady()
{
    // 清空旧的动画/皮肤按钮
    auto clearGrid = [](QGridLayout *grid) {
        while (grid->count() > 0) {
            auto *item = grid->takeAt(0);
            if (auto *w = item->widget()) {
                grid->removeWidget(w);
                w->deleteLater();
            }
            delete item;
        }
    };
    clearGrid(m_animGrid);
    clearGrid(m_skinGrid);

    const auto anims = m_spine->animations();
    qDebug() << "QSpineWidgetDemo animations:" << anims;
    // Spine 多轨道语义: 高轨道覆盖低轨道关键帧, 可同时叠加多个动画。
    // animations() 按轨道优先级从高到低返回(第一个=最高轨道, 最后一个=track 0 基础),
    // UI 从上到下每个动画一个 CheckBox: 勾选=在该轨道循环播放(叠加), 取消=清空该轨道。
    // track 0(列表最后一个, 基础动画)默认常驻。
    int track = static_cast<int>(anims.size()) - 1;
    int row = 0;
    // track 0 基础动画: 优先 Idle(标准命名)。本用例 setup 无附件, 必须播放动画
    // (attachment timeline)才有内容, 故默认勾选并播放 Idle。
    const QString baseAnim = anims.contains(QStringLiteral("Idle"))
            ? QStringLiteral("Idle") : QString();
    for (const QString &name : anims) {
        const int t = track--;
        auto *cb = new QCheckBox(name, this);
        if (name == baseAnim) {
            cb->setChecked(true);
            m_spine->setAnimation(t, name, true);
        }
        connect(cb, &QCheckBox::toggled, this, [this, name, t](bool on) {
            if (on)
                m_spine->setAnimation(t, name, true);
            else
                m_spine->clearTrack(t);
        });
        m_animGrid->addWidget(cb, row++, 0);
    }

    // 皮肤: 互斥 RadioButton(当前皮肤唯一)。初始选中与库侧 currentSkin() 一致
    // (库在 loadResource 自动应用附件最多的皮肤, 通常为角色主皮肤)。
    if (m_skinGroup) { delete m_skinGroup; m_skinGroup = nullptr; }
    m_skinGroup = new QButtonGroup(this);
    m_skinGroup->setExclusive(true);
    const auto skins = m_spine->skins();
    const QString current = m_spine->currentSkin();
    const QString initialSkin = (not current.isEmpty() && skins.contains(current))
            ? current : (skins.isEmpty() ? QString() : skins.first());
    int skinId = 0;
    row = 0;
    for (const QString &name : skins) {
        auto *rb = new QRadioButton(name, this);
        m_skinGroup->addButton(rb, skinId++);
        if (name == initialSkin)
            rb->setChecked(true);
        connect(rb, &QRadioButton::toggled, this, [this, name](bool on) {
            if (on)
                m_spine->setSkin(name);
        });
        m_skinGrid->addWidget(rb, row++, 0);
    }

    // GPU 信息在窗口首次 GL 初始化后才就绪, 延迟刷新一次状态栏以便显示
    QTimer::singleShot(800, this, [this]() { updateStatus(); });
}

void DemoWindow::onLoadFailed()
{
    if (m_statusLabel)
        m_statusLabel->setText(QStringLiteral("资源加载失败: 请确认 examples/your_spine 资源已部署"));
}

void DemoWindow::updateStatus()
{
    if (!m_statusLabel)
        return;
    if (m_spine->isSkeletonReady()) {
        // GPU 信息(确认跑在集显还是独显): GL 上下文首次初始化后才有值
        const QString gpu = m_spine->gpuVendor().isEmpty()
                ? QStringLiteral("(待初始化)")
                : QStringLiteral("%1 / %2").arg(m_spine->gpuVendor(), m_spine->gpuRenderer());
        const QString autoFpsText = m_spine->isAutoFpsEnable()
                ? QStringLiteral(" | AutoFPS: %1").arg(m_spine->getAutoFps(), 0, 'f', 1)
                : QString();
        m_statusLabel->setText(QStringLiteral("已加载: 动画 %1 个, 皮肤 %2 个\nGPU: %3%4")
                               .arg(m_spine->animations().size())
                               .arg(m_spine->skins().size())
                               .arg(gpu)
                               .arg(autoFpsText));
    }
}

bool DemoWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    Q_UNUSED(eventType)
    MSG *msg = static_cast<MSG *>(message);
    // 无边框时仅 Spine 的透明像素(空白)返回 HTTRANSPARENT, 骨架本体保持可交互;
    // 真正跨进程/到桌面的穿透由 startClickThrough() 的 WS_EX_TRANSPARENT 完成,
    // 这里是额外的一层(同线程场景下的双保险)。
    if (msg->message == WM_NCHITTEST && m_frameless && m_spine) {
        // 用 QCursor::pos()(Qt 逻辑坐标, DPI-aware) 代替 WM_NCHITTEST 的物理像素 lParam:
        // 高 DPI 下物理像素与 Qt 逻辑坐标相差 devicePixelRatio 倍, 直接混用会让命中区域
        // 偏移(表现为只有左上角能穿透)。mapFromGlobal/rect/hitTest 全用逻辑坐标保持一致。
        const QPoint local = m_spine->mapFromGlobal(QCursor::pos());
        // 骨架本体(不透明像素)保持可交互, 只有空白/透明像素才穿透
        if (m_spine->rect().contains(local) && !m_spine->hitTest(local)) {
            *result = HTTRANSPARENT;
            return true;
        }
    }
#else
    Q_UNUSED(eventType) Q_UNUSED(message) Q_UNUSED(result)
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

// ---- 无边框点击穿透 ----
// WS_EX_TRANSPARENT 让整窗的鼠标命中测试穿透到下层窗口(跨进程/桌面), 但它是整窗属性。
// 因此用定时器按光标位置动态开/关: 光标落在 Spine 的"透明像素"(空白, 未画骨架)上 ->
// 开启穿透; 移到骨架本体或右侧面板 -> 关闭恢复可点。是否透明由 SpineWidget::hitTest()
// 按帧缓冲 alpha 逐像素判断, 与渲染内容完全一致。
// 用定时器轮询光标(QCursor::pos(), Qt 逻辑坐标, DPI-aware)而不是靠鼠标事件:
// 穿透后窗口收不到鼠标消息, 无法用事件跟踪。
void DemoWindow::startClickThrough()
{
    if (!m_clickThroughTimer) {
        m_clickThroughTimer = new QTimer(this);
        m_clickThroughTimer->setInterval(40);
        connect(m_clickThroughTimer, &QTimer::timeout, this, [this]() { updateClickThrough(); });
    }
    m_clickThroughTimer->start();
    updateClickThrough();
}

void DemoWindow::stopClickThrough()
{
    if (m_clickThroughTimer)
        m_clickThroughTimer->stop();
#ifdef Q_OS_WIN
    if (const HWND h = reinterpret_cast<HWND>(winId())) {
        const LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
        SetWindowLongPtrW(h, GWL_EXSTYLE, ex & ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT));
    }
#endif
}

void DemoWindow::updateClickThrough()
{
#ifdef Q_OS_WIN
    const HWND h = reinterpret_cast<HWND>(winId());
    if (!h)
        return;
    // GetCursorPos 返回物理屏幕像素, 与 Qt 的逻辑坐标(200% 缩放时相差 2 倍)混用会导致
    // 命中区域偏移(表现为只有左上角能穿透); 改用 QCursor::pos() 拿到 DPI-aware 的逻辑坐标。
    const QPoint local = m_spine->mapFromGlobal(QCursor::pos());
    // 仅当光标落在 Spine 的"透明像素"(空白, 未画骨架)上才开启整窗穿透;
    // 落在骨架本体或右侧面板上保持可交互。
    bool want = false;
    if (m_frameless && m_spine->rect().contains(local))
        want = !m_spine->hitTest(local);
    const LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (want && !(ex & WS_EX_TRANSPARENT))
        SetWindowLongPtrW(h, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT);
    else if (!want && (ex & WS_EX_TRANSPARENT))
        SetWindowLongPtrW(h, GWL_EXSTYLE, ex & ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT));
#else
    Q_UNUSED(this)
#endif
}
