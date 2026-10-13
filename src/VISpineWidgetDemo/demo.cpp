#include "demo.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFrame>
#include <QPushButton>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QTimer>
#include <QSlider>
#include <QScrollArea>
#include <QColorDialog>
#include <QDebug>
#include <QCursor>
#include <QEvent>
#include <QSet>
#ifdef Q_OS_WIN
#  include <windows.h>
#endif
#include "spinewidget.h"

// 已应用轨道每行固定高度(px), 容器高度按行数计算
static constexpr int kAppliedRowHeight = 24;

DemoWindow::DemoWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("VISpineWidget Demo - QtWidgets (Plugin UI)"));
    resize(1280, 800);

    // 窗口透明: QOpenGLWidget 的 alpha 需要顶层窗口也设 WA_TranslucentBackground,
    // 否则 GL 透明背景(点"透明"按钮)会被 Qt 合成成黑色, 无法透出桌面/下层窗口。
    // 左竖条/右面板各自画实色(#f0f2f5), 中间 Spine 默认黑底; 点"透明"即透出桌面,
    // 配合"无边框"即可把角色当桌宠用。
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);

    // ---- 中间: Spine 渲染区 ----
    m_spine = new Visindigo::Widgets::SpineWidget(this);
    m_spine->setMinimumSize(480, 480);
    m_spine->setBackgroundColor(QColor("#000000"));
    m_spine->setAtlasFile(QStringLiteral("examples/tsingyayin/tsingyayin.atlas"));
    m_spine->setSkeletonFile(QStringLiteral("examples/tsingyayin/tsingyayin.json"));
    m_spine->setFps(60);
    m_spine->setDefaultMix(0.2);

    // ---- 左侧竖条: 重置 + 自适应帧率 + 调试 + 背景 + 已加载 + GPU ----
    auto *leftBar = new QWidget(this);
    m_leftBar = leftBar;
    leftBar->setFixedWidth(150);
    QPalette lp = leftBar->palette();
    lp.setColor(QPalette::Window, QColor("#f0f2f5"));
    leftBar->setPalette(lp);
    leftBar->setAutoFillBackground(true);
    auto *leftLayout = new QVBoxLayout(leftBar);
    leftLayout->setContentsMargins(6, 6, 6, 6);
    leftLayout->setSpacing(10);

    // 无边框模式下: 顶部"收起左栏"按钮(默认隐藏)
    m_leftCollapseBtn = new QPushButton(QStringLiteral("◀ 收起左栏"), leftBar);
    m_leftCollapseBtn->setVisible(false);
    connect(m_leftCollapseBtn, &QPushButton::clicked, this, [this]() {
        m_leftVisible = false;
        relayoutFloating();
    });
    leftLayout->insertWidget(0, m_leftCollapseBtn);

    // 重置姿态(回到 setup 并从头重播已应用轨道)
    auto *resetBtn = new QPushButton(QStringLiteral("重置姿态"), leftBar);
    connect(resetBtn, &QPushButton::clicked, this, [this]() {
        m_spine->setToSetupPose();
        syncAppliedTracks();
    });
    leftLayout->addWidget(resetBtn);

    // 自适应帧率
    auto *autoFpsCb = new QCheckBox(QStringLiteral("自适应帧率"), leftBar);
    connect(autoFpsCb, &QCheckBox::toggled, this, [this](bool on) { m_spine->setAutoFpsEnable(on); });
    leftLayout->addWidget(autoFpsCb);

    // 调试
    auto *debugBox = new QGroupBox(QStringLiteral("调试"), leftBar);
    auto *debugLayout = new QVBoxLayout(debugBox);
    m_debugBones = new QCheckBox(QStringLiteral("骨骼"), debugBox);
    m_debugSlots = new QCheckBox(QStringLiteral("插槽"), debugBox);
    m_debugMesh = new QCheckBox(QStringLiteral("网格"), debugBox);
    connect(m_debugBones, &QCheckBox::toggled, this, [this](bool on) { m_spine->setDebugBones(on); });
    connect(m_debugSlots, &QCheckBox::toggled, this, [this](bool on) { m_spine->setDebugSlots(on); });
    connect(m_debugMesh, &QCheckBox::toggled, this, [this](bool on) { m_spine->setDebugMesh(on); });
    debugLayout->addWidget(m_debugBones);
    debugLayout->addWidget(m_debugSlots);
    debugLayout->addWidget(m_debugMesh);
    // 线条宽度(统一三种调试线宽)
    auto *widthLabel = new QLabel(QStringLiteral("线条宽度"), debugBox);
    auto *widthRow = new QHBoxLayout;
    auto *widthSlider = new QSlider(Qt::Horizontal, debugBox);
    widthSlider->setRange(1, 5);
    widthSlider->setValue(2);
    auto *widthVal = new QLabel(QStringLiteral("2"), debugBox);
    connect(widthSlider, &QSlider::valueChanged, this, [this, widthVal](int v) {
        m_spine->setDebugLineWidth(v);
        widthVal->setText(QString::number(v));
    });
    widthRow->addWidget(widthSlider, 1);
    widthRow->addWidget(widthVal);
    debugLayout->addWidget(widthLabel);
    debugLayout->addLayout(widthRow);
    m_spine->setDebugLineWidth(2);
    leftLayout->addWidget(debugBox);

    // 背景(垂直)
    auto *bgBox = new QGroupBox(QStringLiteral("背景"), leftBar);
    auto *bgLayout = new QVBoxLayout(bgBox);
    auto addBg = [&](const QString &text, const QColor &color) {
        auto *btn = new QPushButton(text, bgBox);
        connect(btn, &QPushButton::clicked, this, [this, color]() { m_spine->setBackgroundColor(color); });
        bgLayout->addWidget(btn);
    };
    addBg(QStringLiteral("透明"), QColor(0, 0, 0, 0));
    addBg(QStringLiteral("黑色"), QColor("#000000"));
    addBg(QStringLiteral("灰色"), QColor("#808080"));
    addBg(QStringLiteral("白色"), QColor("#ffffff"));
    auto *customBtn = new QPushButton(QStringLiteral("自定义..."), bgBox);
    connect(customBtn, &QPushButton::clicked, this, [this]() {
        const QColor color = QColorDialog::getColor(m_spine->backgroundColor(), this, QStringLiteral("选择背景色"));
        if (color.isValid())
            m_spine->setBackgroundColor(color);
    });
    bgLayout->addWidget(customBtn);
    leftLayout->addWidget(bgBox);

    // 已加载内容(3 行: 动画/皮肤/FPS)
    m_statusLabel = new QLabel(QStringLiteral("等待加载..."), leftBar);
    m_statusLabel->setWordWrap(true);
    leftLayout->addWidget(m_statusLabel);

    leftLayout->addStretch(1);

    // GPU(左竖条底部)
    m_gpuLabel = new QLabel(QStringLiteral("GPU 待初始化"), leftBar);
    m_gpuLabel->setWordWrap(true);
    m_gpuLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    leftLayout->addWidget(m_gpuLabel);

    // ---- 右侧控制面板 ----
    auto *rightPanel = new QWidget(this);
    m_rightPanel = rightPanel;
    rightPanel->setFixedWidth(400);
    QPalette rp = rightPanel->palette();
    rp.setColor(QPalette::Window, QColor("#f0f2f5"));
    rightPanel->setPalette(rp);
    rightPanel->setAutoFillBackground(true);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    // 无边框模式下: 顶部"收起右栏"按钮(默认隐藏)
    m_rightCollapseBtn = new QPushButton(QStringLiteral("收起右栏 ▶"), rightPanel);
    m_rightCollapseBtn->setVisible(false);
    connect(m_rightCollapseBtn, &QPushButton::clicked, this, [this]() {
        m_rightVisible = false;
        relayoutFloating();
    });
    rightLayout->insertWidget(0, m_rightCollapseBtn);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setSpacing(10);

    // 皮肤
    auto *skinBox = new QGroupBox(QStringLiteral("皮肤"), content);
    auto *skinLayout = new QVBoxLayout(skinBox);
    m_skinGrid = new QGridLayout;
    m_skinGrid->setSpacing(6);
    skinLayout->addLayout(m_skinGrid);
    layout->addWidget(skinBox);

    // 动画: 上半"已应用"(表头 + 行) + 下半"选择动画"(2 列按钮)
    auto *animBox = new QGroupBox(QStringLiteral("动画"), content);
    auto *animLayout = new QVBoxLayout(animBox);

    // 表头(动画名 | 循环 | 层级), 与行内列对齐
    auto *appliedHeader = new QWidget(animBox);
    auto *headerLay = new QHBoxLayout(appliedHeader);
    headerLay->setContentsMargins(2, 0, 2, 0);
    headerLay->setSpacing(4);
    auto *hName = new QLabel(QStringLiteral("动画名"), appliedHeader);
    auto *hLoop = new QLabel(QStringLiteral("循环"), appliedHeader);
    hLoop->setFixedWidth(28);
    hLoop->setAlignment(Qt::AlignCenter);
    auto *hLevel = new QLabel(QStringLiteral("层级"), appliedHeader);
    hLevel->setFixedWidth(56);
    hLevel->setAlignment(Qt::AlignCenter);
    headerLay->addWidget(hName, 1);
    headerLay->addWidget(hLoop);
    headerLay->addWidget(hLevel);
    animLayout->addWidget(appliedHeader);

    // 已应用容器(纯 QFrame 圆角)
    m_appliedContainer = makeRoundedFrame(animBox);
    m_appliedRows = new QVBoxLayout(m_appliedContainer);
    m_appliedRows->setContentsMargins(6, 4, 6, 4);
    m_appliedRows->setSpacing(2);
    animLayout->addWidget(m_appliedContainer);
    updateAppliedContainerSize();

    // 选择动画容器(圆角 + 滚动 + 2 列按钮)
    auto *availableLabel = new QLabel(QStringLiteral("选择动画"), animBox);
    m_availableContainer = makeRoundedFrame(animBox);
    auto *availLay = new QVBoxLayout(m_availableContainer);
    availLay->setContentsMargins(6, 4, 6, 4);
    auto *availScroll = new QScrollArea(m_availableContainer);
    availScroll->setFrameShape(QFrame::NoFrame);
    availScroll->setWidgetResizable(true);
    availScroll->viewport()->setAutoFillBackground(false);
    availLay->addWidget(availScroll);
    m_availableHost = new QWidget(availScroll);
    m_availableHost->setAutoFillBackground(false);
    m_availableGrid = new QGridLayout(m_availableHost);
    m_availableGrid->setContentsMargins(0, 0, 0, 0);
    m_availableGrid->setSpacing(4);
    availScroll->setWidget(m_availableHost);
    m_availableContainer->setMaximumHeight(220);
    m_availableContainer->setMinimumHeight(90);
    animLayout->addWidget(availableLabel);
    animLayout->addWidget(m_availableContainer);

    layout->addWidget(animBox);

    // 属性
    auto *propBox = new QGroupBox(QStringLiteral("属性"), content);
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
    QSlider *fpsSlider = makeSlider(1, 240, 60);
    connect(fpsSlider, &QSlider::valueChanged, this, [this, fpsVal](int v) { m_spine->setFps(v); fpsVal->setText(QString::number(v)); });
    addPropRow(QStringLiteral("FPS"), fpsSlider, fpsVal);

    QLabel *scaleVal = new QLabel(QStringLiteral("0.50"), propBox);
    QSlider *scaleSlider = makeSlider(10, 200, 50);
    connect(scaleSlider, &QSlider::valueChanged, this, [this, scaleVal](int v) {
        if (m_uiSyncing) return;
        m_spine->setSkeletonScale(v / 100.0);
        scaleVal->setText(QString::number(v / 100.0, 'f', 2));
    });
    addPropRow(QStringLiteral("缩放"), scaleSlider, scaleVal);
    m_spine->setSkeletonScale(0.5);
    // 滚轮缩放 -> 缩放滑块同步
    connect(m_spine, &Visindigo::Widgets::SpineWidget::skeletonScaleChanged, this, [this, scaleSlider, scaleVal](qreal v) {
        if (m_uiSyncing) return;
        m_uiSyncing = true;
        scaleSlider->setValue(qRound(v * 100.0));
        scaleVal->setText(QString::number(v, 'f', 2));
        m_uiSyncing = false;
    });

    QLabel *timeVal = new QLabel(QStringLiteral("1.00"), propBox);
    QSlider *timeSlider = makeSlider(10, 200, 100);
    connect(timeSlider, &QSlider::valueChanged, this, [this, timeVal](int v) { m_spine->setTimeScale(v / 100.0); timeVal->setText(QString::number(v / 100.0, 'f', 2)); });
    addPropRow(QStringLiteral("时间倍率"), timeSlider, timeVal);

    QLabel *xVal = new QLabel(QStringLiteral("1.00"), propBox);
    QSlider *xSlider = makeSlider(-200, 200, 100);
    connect(xSlider, &QSlider::valueChanged, this, [this, xVal](int v) { m_spine->setScaleX(v / 100.0); xVal->setText(QString::number(v / 100.0, 'f', 2)); });
    addPropRow(QStringLiteral("X 缩放"), xSlider, xVal);

    QLabel *yVal = new QLabel(QStringLiteral("1.00"), propBox);
    QSlider *ySlider = makeSlider(-200, 200, 100);
    connect(ySlider, &QSlider::valueChanged, this, [this, yVal](int v) { m_spine->setScaleY(v / 100.0); yVal->setText(QString::number(v / 100.0, 'f', 2)); });
    addPropRow(QStringLiteral("Y 缩放"), ySlider, yVal);

    QLabel *anchorXVal = new QLabel(QStringLiteral("0.00"), propBox);
    QSlider *anchorXSlider = makeSlider(-500, 500, 0);
    connect(anchorXSlider, &QSlider::valueChanged, this, [this, anchorXVal](int v) {
        if (m_uiSyncing) return;
        m_spine->setAnchorOffset(QPointF(v / 100.0, m_spine->anchorOffset().y()));
        anchorXVal->setText(QString::number(v / 100.0, 'f', 2));
    });
    addPropRow(QStringLiteral("原点X偏移"), anchorXSlider, anchorXVal);

    QLabel *anchorYVal = new QLabel(QStringLiteral("1.00"), propBox);
    QSlider *anchorYSlider = makeSlider(-500, 500, 100);
    connect(anchorYSlider, &QSlider::valueChanged, this, [this, anchorYVal](int v) {
        if (m_uiSyncing) return;
        m_spine->setAnchorOffset(QPointF(m_spine->anchorOffset().x(), v / 100.0));
        anchorYVal->setText(QString::number(v / 100.0, 'f', 2));
    });
    addPropRow(QStringLiteral("原点Y偏移"), anchorYSlider, anchorYVal);
    m_spine->setAnchorOffset(QPointF(m_spine->anchorOffset().x(), 1.0));
    // 拖动/滚轮产生的 anchorOffset 变化 -> XY 滑块同步
    connect(m_spine, &Visindigo::Widgets::SpineWidget::anchorOffsetChanged, this, [this, anchorXSlider, anchorXVal, anchorYSlider, anchorYVal](const QPointF &off) {
        if (m_uiSyncing) return;
        m_uiSyncing = true;
        anchorXSlider->setValue(qRound(off.x() * 100.0));
        anchorXVal->setText(QString::number(off.x(), 'f', 2));
        anchorYSlider->setValue(qRound(off.y() * 100.0));
        anchorYVal->setText(QString::number(off.y(), 'f', 2));
        m_uiSyncing = false;
    });

    QLabel *mixVal = new QLabel(QStringLiteral("0.20"), propBox);
    QSlider *mixSlider = makeSlider(0, 200, 20);
    connect(mixSlider, &QSlider::valueChanged, this, [this, mixVal](int v) { m_spine->setDefaultMix(v / 100.0); mixVal->setText(QString::number(v / 100.0, 'f', 2)); });
    addPropRow(QStringLiteral("混合时长"), mixSlider, mixVal);
    layout->addWidget(propBox);

    // 窗口(Demo 特有: 无边框点击穿透)
    auto *winBox = new QGroupBox(QStringLiteral("窗口"), content);
    auto *winLayout = new QVBoxLayout(winBox);
    auto *framelessCb = new QCheckBox(QStringLiteral("无边框(点击穿透)"), winBox);
    connect(framelessCb, &QCheckBox::toggled, this, [this](bool on) {
        m_frameless = on;
        if (on) {
            // 无边框 = 桌宠模式: 记录背景色并应用透明, 面板切换为悬浮覆盖
            m_bgBeforeFrameless = m_spine->backgroundColor();
            m_spine->setBackgroundColor(QColor(0, 0, 0, 0));
            startClickThrough();
        } else {
            stopClickThrough();
            m_spine->setBackgroundColor(m_bgBeforeFrameless);
        }
        m_spine->setClickThrough(on);   // 库侧命中测试也置为穿透(双保险)
        setFloatingMode(on);
        const bool wasVisible = isVisible();
        setWindowFlag(Qt::FramelessWindowHint, on);
        if (wasVisible) {
            showMinimized();
            QTimer::singleShot(0, this, [this]() { showNormal(); });
        }
        relayoutFloating();
    });
    winLayout->addWidget(framelessCb);
    layout->addWidget(winBox);

    layout->addStretch(1);
    scroll->setWidget(content);
    rightLayout->addWidget(scroll);

    // ---- 主布局: 左竖条 | 渲染区 | 右侧面板 ----
    auto *mainLayout = new QHBoxLayout(this);
    m_mainLayout = mainLayout;
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);
    mainLayout->addWidget(leftBar);
    mainLayout->addWidget(m_spine, 1);
    mainLayout->addWidget(rightPanel);

    // 悬浮展开柄(无边框且某栏收起时, 浮在 Spine 边缘点击展开)
    m_leftTab = new QPushButton(QStringLiteral("»"), this);
    m_leftTab->setFixedSize(26, 60);
    m_leftTab->setVisible(false);
    connect(m_leftTab, &QPushButton::clicked, this, [this]() { m_leftVisible = true; relayoutFloating(); });
    m_rightTab = new QPushButton(QStringLiteral("«"), this);
    m_rightTab->setFixedSize(26, 60);
    m_rightTab->setVisible(false);
    connect(m_rightTab, &QPushButton::clicked, this, [this]() { m_rightVisible = true; relayoutFloating(); });

    connect(m_spine, &Visindigo::Widgets::SpineWidget::resourceReady, this, &DemoWindow::onResourceReady);
    connect(m_spine, &Visindigo::Widgets::SpineWidget::resourceLoadFailed, this, &DemoWindow::onLoadFailed);
    connect(m_spine, &Visindigo::Widgets::SpineWidget::resourceReady, this, &DemoWindow::updateStatus);

    m_statusTimer = new QTimer(this);
    m_statusTimer->setInterval(500);
    connect(m_statusTimer, &QTimer::timeout, this, &DemoWindow::updateStatus);
    m_statusTimer->start();
}

QSlider *DemoWindow::makeSlider(int from, int to, double value)
{
    auto *slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(from, to);
    slider->setValue(static_cast<int>(value));
    slider->setFixedWidth(190);
    return slider;
}

QFrame *DemoWindow::makeRoundedFrame(QWidget *parent)
{
    auto *frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("roundedBox"));
    frame->setStyleSheet(QStringLiteral("#roundedBox { background: rgba(255,255,255,0.65); border: 1px solid #b0b0b0; border-radius: 6px; }"));
    return frame;
}

void DemoWindow::onResourceReady()
{
    // 清空旧的皮肤按钮
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
    clearGrid(m_skinGrid);

    // 清空已应用列表
    while (m_appliedRows->count() > 0) {
        auto *item = m_appliedRows->takeAt(0);
        if (auto *w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_spine->clearTracks();
    rebuildAvailableGrid();

    // 皮肤: 互斥 RadioButton(当前皮肤唯一)。初始选中与库侧 currentSkin() 一致
    if (m_skinGroup) { delete m_skinGroup; m_skinGroup = nullptr; }
    m_skinGroup = new QButtonGroup(this);
    m_skinGroup->setExclusive(true);
    const auto skins = m_spine->skins();
    const QString current = m_spine->currentSkin();
    const QString initialSkin = (not current.isEmpty() && skins.contains(current))
            ? current : (skins.isEmpty() ? QString() : skins.first());
    int skinId = 0;
    int row = 0;
    for (const QString &name : skins) {
        auto *rb = new QRadioButton(name, this);
        m_skinGroup->addButton(rb, skinId++);
        if (name == initialSkin)
            rb->setChecked(true);
        connect(rb, &QRadioButton::toggled, this, [this, name](bool on) {
            if (on) m_spine->setSkin(name);
        });
        m_skinGrid->addWidget(rb, row++, 0);
    }

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
        const double fpsValue = m_spine->isAutoFpsEnable() ? m_spine->getAutoFps() : double(m_spine->fps());
        m_statusLabel->setText(QStringLiteral("动画: %1 个\n皮肤: %2 个\nFPS: %3")
                               .arg(m_spine->animations().size())
                               .arg(m_spine->skins().size())
                               .arg(QString::number(fpsValue, 'f', 1)));
        if (m_gpuLabel) {
            m_gpuLabel->setText(m_spine->gpuVendor().isEmpty()
                                ? QStringLiteral("GPU 待初始化")
                                : QStringLiteral("GPU: %1 / %2").arg(m_spine->gpuVendor(), m_spine->gpuRenderer()));
        }
    }
}

// ---- 已应用轨道 ----
QWidget *DemoWindow::makeAppliedRow(const QString &name, bool loop)
{
    auto *row = new QWidget(m_appliedContainer);
    row->setProperty("animName", name);
    row->setFixedHeight(kAppliedRowHeight);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(4);
    auto *nameLabel = new QLabel(name, row);
    auto *loopCb = new QCheckBox(row);
    loopCb->setChecked(loop);
    loopCb->setFixedWidth(28);
    auto *upBtn = new QPushButton(QStringLiteral("↑"), row);
    auto *downBtn = new QPushButton(QStringLiteral("↓"), row);
    upBtn->setFixedWidth(26);
    downBtn->setFixedWidth(26);
    h->addWidget(nameLabel, 1);
    h->addWidget(loopCb);
    h->addWidget(upBtn);
    h->addWidget(downBtn);
    connect(loopCb, &QCheckBox::toggled, this, [this, row](bool on) {
        const int idx = m_appliedRows->indexOf(row);
        if (idx >= 0)
            m_spine->setAnimation(idx, row->property("animName").toString(), on);
    });
    connect(upBtn, &QPushButton::clicked, this, [this, row]() {
        const int idx = m_appliedRows->indexOf(row);
        if (idx > 0) moveAppliedRow(idx, idx - 1);
    });
    connect(downBtn, &QPushButton::clicked, this, [this, row]() {
        const int idx = m_appliedRows->indexOf(row);
        if (idx >= 0 && idx < m_appliedRows->count() - 1) moveAppliedRow(idx, idx + 1);
    });
    row->installEventFilter(this);
    return row;
}

void DemoWindow::addToApplied(const QString &name)
{
    if (name.isEmpty()) return;
    m_appliedRows->addWidget(makeAppliedRow(name, true));
    updateAppliedContainerSize();
    rebuildAvailableGrid();
    syncAppliedTracks();
}

void DemoWindow::removeFromApplied(QWidget *row)
{
    if (!row) return;
    const int idx = m_appliedRows->indexOf(row);
    if (idx < 0) return;
    m_appliedRows->takeAt(idx);
    row->deleteLater();
    updateAppliedContainerSize();
    rebuildAvailableGrid();
    syncAppliedTracks();
}

void DemoWindow::moveAppliedRow(int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= m_appliedRows->count()) return;
    QLayoutItem *item = m_appliedRows->takeAt(from);
    if (item) m_appliedRows->insertItem(to, item);
    syncAppliedTracks();
}

void DemoWindow::rebuildAvailableGrid()
{
    if (!m_availableGrid) return;
    QSet<QString> applied;
    for (int i = 0; i < m_appliedRows->count(); ++i) {
        if (auto *row = m_appliedRows->itemAt(i)->widget())
            applied.insert(row->property("animName").toString());
    }
    while (auto *item = m_availableGrid->takeAt(0)) {
        if (auto *w = item->widget()) w->deleteLater();
        delete item;
    }
    const auto anims = m_spine->animations();
    int idx = 0;
    for (const QString &name : anims) {
        if (applied.contains(name)) continue;
        auto *btn = new QPushButton(name, m_availableHost);
        connect(btn, &QPushButton::clicked, this, [this, name]() { addToApplied(name); });
        m_availableGrid->addWidget(btn, idx / 2, idx % 2);
        ++idx;
    }
}

void DemoWindow::syncAppliedTracks()
{
    if (!m_spine->isSkeletonReady()) return;
    m_spine->clearTracks();
    for (int i = 0; i < m_appliedRows->count(); ++i) {
        auto *row = m_appliedRows->itemAt(i)->widget();
        if (!row) continue;
        QCheckBox *cb = row->findChild<QCheckBox *>();
        m_spine->setAnimation(i, row->property("animName").toString(), cb ? cb->isChecked() : true);
    }
}

void DemoWindow::updateAppliedContainerSize()
{
    if (!m_appliedContainer || !m_appliedRows) return;
    const int n = m_appliedRows->count();
    const QMargins m = m_appliedRows->contentsMargins();
    const int spacing = m_appliedRows->spacing();
    const int h = m.top() + m.bottom() + n * kAppliedRowHeight + (n > 0 ? (n - 1) * spacing : 0);
    m_appliedContainer->setFixedHeight(qMax(h, 60));
}

bool DemoWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonDblClick) {
        auto *w = qobject_cast<QWidget *>(obj);
        if (w && m_appliedRows && m_appliedRows->indexOf(w) >= 0) {
            removeFromApplied(w);
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void DemoWindow::setFloatingMode(bool floating)
{
    m_floating = floating;
    if (floating) {
        // 面板脱离布局, 悬浮覆盖在整页 Spine 之上
        m_mainLayout->removeWidget(m_leftBar);
        m_mainLayout->removeWidget(m_rightPanel);
        m_leftCollapseBtn->setVisible(true);
        m_rightCollapseBtn->setVisible(true);
        m_leftVisible = true;
        m_rightVisible = true;
        relayoutFloating();
    } else {
        m_mainLayout->insertWidget(0, m_leftBar);
        m_mainLayout->addWidget(m_rightPanel);
        m_leftCollapseBtn->setVisible(false);
        m_rightCollapseBtn->setVisible(false);
        m_leftTab->setVisible(false);
        m_rightTab->setVisible(false);
        m_leftBar->setVisible(true);
        m_rightPanel->setVisible(true);
    }
}

void DemoWindow::relayoutFloating()
{
    if (!m_floating)
        return;
    const int w = width();
    const int h = height();
    if (m_leftVisible) {
        m_leftBar->setGeometry(0, 0, 150, h);
        m_leftBar->setVisible(true);
        m_leftBar->raise();
    } else {
        m_leftBar->setVisible(false);
    }
    if (m_rightVisible) {
        m_rightPanel->setGeometry(w - 400, 0, 400, h);
        m_rightPanel->setVisible(true);
        m_rightPanel->raise();
    } else {
        m_rightPanel->setVisible(false);
    }
    // 收起时浮在边缘的展开柄
    m_leftTab->setVisible(!m_leftVisible);
    m_rightTab->setVisible(!m_rightVisible);
    const int tabW = 26, tabH = 60;
    m_leftTab->setGeometry(0, h / 2 - tabH / 2, tabW, tabH);
    m_rightTab->setGeometry(w - tabW, h / 2 - tabH / 2, tabW, tabH);
    m_leftTab->raise();
    m_rightTab->raise();
}

bool DemoWindow::overInteractiveUi(const QPoint &globalPos)
{
    auto containsWidget = [&](QWidget *w) {
        if (!w->isVisible())
            return false;
        const QRect g(w->mapToGlobal(QPoint(0, 0)), w->size());
        return g.contains(globalPos);
    };
    if (containsWidget(m_leftBar)) return true;
    if (containsWidget(m_rightPanel)) return true;
    if (containsWidget(m_leftTab)) return true;
    if (containsWidget(m_rightTab)) return true;
    return false;
}

void DemoWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayoutFloating();
}

bool DemoWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    Q_UNUSED(eventType)
    MSG *msg = static_cast<MSG *>(message);
    // 无边框时仅 Spine 的透明像素(空白)返回 HTTRANSPARENT, 骨架本体保持可交互;
    // 真正跨进程/到桌面的穿透由 startClickThrough() 的 WS_EX_TRANSPARENT 完成。
    if (msg->message == WM_NCHITTEST && m_frameless && m_spine) {
        const QPoint local = m_spine->mapFromGlobal(QCursor::pos());
        if (m_spine->rect().contains(local) && !overInteractiveUi(QCursor::pos()) && !m_spine->hitTest(local)) {
            *result = HTTRANSPARENT;
            return true;
        }
    }
#else
    Q_UNUSED(eventType) Q_UNUSED(message) Q_UNUSED(result)
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

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
    const QPoint local = m_spine->mapFromGlobal(QCursor::pos());
    bool want = false;
    if (m_frameless && m_spine->rect().contains(local) && !overInteractiveUi(QCursor::pos()))
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
