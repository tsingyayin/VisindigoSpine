#include "SpineViewer/SpineViewerWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QLabel>
#include <QTimer>
#include <QSlider>
#include <QScrollArea>
#include <QListWidget>
#include <QEvent>
#include <QDebug>
#include <QColorDialog>
#include <QFileInfo>
#include <QPoint>
#include <QSet>
#include <General/TranslationHost.h>
#include <Widgets/BorderFrame.h>
#include "spinewidget.h"

namespace YSS::SpineViewer {
	// 已应用轨道每行固定高度(px), 容器高度按行数计算
	constexpr int kAppliedRowHeight = 24;

	class SpineViewerWidgetPrivate {
		friend class SpineViewerWidget;
	protected:
		Visindigo::Widgets::SpineWidget* Spine = nullptr;
		Visindigo::Widgets::BorderFrame* AppliedContainer = nullptr;  // 已应用容器(圆角边框, 每行 = [名字|循环|上移|下移])
		QVBoxLayout* AppliedRowsLayout = nullptr;
		Visindigo::Widgets::BorderFrame* AvailableContainer = nullptr; // 选择动画容器(圆角边框)
		QScrollArea* AvailableScroll = nullptr;     // 选择动画滚动区
		QWidget* AvailableGridHost = nullptr;       // 网格宿主
		QGridLayout* AvailableGrid = nullptr;       // 选择动画 2 列按钮网格
		QGridLayout* SkinGrid = nullptr;
		QButtonGroup* SkinGroup = nullptr;     // 皮肤互斥组(当前皮肤唯一)
		QTimer* StatusTimer = nullptr;         // 状态栏定期刷新(动态数据如 AutoFPS)
		QLabel* StatusLabel = nullptr;   // 已加载内容(左竖条下部, 3 行: 动画/皮肤/AutoFPS)
		QLabel* GpuLabel = nullptr;      // GPU(左竖条底部, 自动换行)
		QCheckBox* DebugBones = nullptr;
		QCheckBox* DebugSlots = nullptr;
		QCheckBox* DebugMesh = nullptr;
		bool Syncing = false;                  // 同步轨道时的重入保护
		bool UiSyncing = false;                // UI 滑块与库信号互同步时的重入保护
	};

	SpineViewerWidget::SpineViewerWidget(QWidget* parent)
		: YSSCore::Editor::FileEditWidget(parent)
		, d(new SpineViewerWidgetPrivate())
	{
		// 注意: 不要再在这里调用 QSurfaceFormat::setDefaultFormat()。
		// ① 库侧 SpineWidget 构造已用 setFormat() 自行设置 alpha+MSAA(自包含);
		// ② setDefaultFormat 应在 QApplication 之前调用, 插件运行时调用会改写 YSS 全局默认
		//    表面格式, 导致首次创建 OpenGL 上下文时 Qt 重建顶层原生窗口 —— Windows 上表现为
		//    "最小化后自动恢复"(与本问题现象一致)。

		// ---- 中间: Spine 渲染区 ----
		d->Spine = new Visindigo::Widgets::SpineWidget(this);
		d->Spine->setMinimumSize(480, 480);
		d->Spine->setBackgroundColor(QColor("#000000"));   // 预览默认黑色背景
		d->Spine->setFps(60);
		d->Spine->setDefaultMix(0.2);

		// ---- 左侧竖条: 重置姿态/自适应帧率(顶部) + 调试 + 背景(垂直布局) ----
		auto* leftBar = new QWidget(this);
		leftBar->setFixedWidth(140);
		leftBar->setAutoFillBackground(true);       // 与右侧统一用 Base 底色(比 Window 浅, 贴近 YSS 编辑器底色)
		leftBar->setBackgroundRole(QPalette::Base);
		auto* leftLayout = new QVBoxLayout(leftBar);
		leftLayout->setContentsMargins(6, 6, 6, 6);
		leftLayout->setSpacing(10);

		// 重置姿态(左竖条最顶部)
		auto* resetBtn = new QPushButton(VI18N("i18n:YSSSpineViewer::reset"), leftBar);
		connect(resetBtn, &QPushButton::clicked, this, [this]() {
			d->Spine->setToSetupPose();      // 回到 setup 姿态
			syncAppliedTracks();             // 重新应用轨道 -> 从头重新播放(而非清空暂停)
			});
		leftLayout->addWidget(resetBtn);

		// 自适应帧率(左竖条顶部)
		auto* autoFpsCb = new QCheckBox(VI18N("i18n:YSSSpineViewer::properties.autoFps"), leftBar);
		connect(autoFpsCb, &QCheckBox::toggled, this, [this](bool on) { d->Spine->setAutoFpsEnable(on); });
		leftLayout->addWidget(autoFpsCb);

		// 调试
		auto* debugBox = new QGroupBox(VI18N("i18n:YSSSpineViewer::debug.title"), leftBar);
		auto* debugLayout = new QVBoxLayout(debugBox);
		d->DebugBones = new QCheckBox(VI18N("i18n:YSSSpineViewer::debug.bones"), debugBox);
		d->DebugSlots = new QCheckBox(VI18N("i18n:YSSSpineViewer::debug.slots"), debugBox);
		d->DebugMesh = new QCheckBox(VI18N("i18n:YSSSpineViewer::debug.mesh"), debugBox);
		connect(d->DebugBones, &QCheckBox::toggled, this, [this](bool on) { d->Spine->setDebugBones(on); });
		connect(d->DebugSlots, &QCheckBox::toggled, this, [this](bool on) { d->Spine->setDebugSlots(on); });
		connect(d->DebugMesh, &QCheckBox::toggled, this, [this](bool on) { d->Spine->setDebugMesh(on); });
		debugLayout->addWidget(d->DebugBones);
		debugLayout->addWidget(d->DebugSlots);
		debugLayout->addWidget(d->DebugMesh);
		// 线条宽度(统一设置三种调试线宽, 颜色保持不变)
		auto* widthLabel = new QLabel(VI18N("i18n:YSSSpineViewer::debug.lineWidth"), debugBox);
		auto* widthRow = new QHBoxLayout;
		auto* widthSlider = new QSlider(Qt::Horizontal, debugBox);
		widthSlider->setRange(1, 5);
		widthSlider->setValue(2);
		auto* widthVal = new QLabel(QStringLiteral("2"), debugBox);
		connect(widthSlider, &QSlider::valueChanged, this, [this, widthVal](int v) {
			d->Spine->setDebugLineWidth(v);
			widthVal->setText(QString::number(v));
			});
		widthRow->addWidget(widthSlider, 1);
		widthRow->addWidget(widthVal);
		debugLayout->addWidget(widthLabel);
		debugLayout->addLayout(widthRow);
		d->Spine->setDebugLineWidth(2);   // 初始统一宽度 2(与滑块一致)
		leftLayout->addWidget(debugBox);

		// 背景(垂直)
		auto* bgBox = new QGroupBox(VI18N("i18n:YSSSpineViewer::background.title"), leftBar);
		auto* bgLayout = new QVBoxLayout(bgBox);
		auto* blackBtn = new QPushButton(VI18N("i18n:YSSSpineViewer::background.black"), bgBox);
		auto* grayBtn = new QPushButton(VI18N("i18n:YSSSpineViewer::background.gray"), bgBox);
		auto* whiteBtn = new QPushButton(VI18N("i18n:YSSSpineViewer::background.white"), bgBox);
		auto* customBtn = new QPushButton(VI18N("i18n:YSSSpineViewer::background.custom"), bgBox);
		connect(blackBtn, &QPushButton::clicked, this, [this]() { d->Spine->setBackgroundColor(QColor("#000000")); });
		connect(grayBtn, &QPushButton::clicked, this, [this]() { d->Spine->setBackgroundColor(QColor("#808080")); });
		connect(whiteBtn, &QPushButton::clicked, this, [this]() { d->Spine->setBackgroundColor(QColor("#ffffff")); });
		connect(customBtn, &QPushButton::clicked, this, [this]() {
			const QColor color = QColorDialog::getColor(d->Spine->backgroundColor(), this, VI18N("i18n:YSSSpineViewer::background.pickTitle"));
			if (color.isValid())
				d->Spine->setBackgroundColor(color);
			});
		bgLayout->addWidget(blackBtn);
		bgLayout->addWidget(grayBtn);
		bgLayout->addWidget(whiteBtn);
		bgLayout->addWidget(customBtn);
		leftLayout->addWidget(bgBox);

		// 已加载内容(左竖条下部, 挨着背景框向下, 3 行: 动画/皮肤/AutoFPS)
		d->StatusLabel = new QLabel(VI18N("i18n:YSSSpineViewer::status.waiting"), leftBar);
		d->StatusLabel->setWordWrap(true);
		leftLayout->addWidget(d->StatusLabel);

		leftLayout->addStretch(1);

		// GPU(左竖条底部, 向上布局; 名字自动换行不超宽)
		d->GpuLabel = new QLabel(VI18N("i18n:YSSSpineViewer::status.gpuPending"), leftBar);
		d->GpuLabel->setWordWrap(true);
		d->GpuLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
		leftLayout->addWidget(d->GpuLabel);

		// ---- 右侧控制面板 ----
		auto* rightPanel = new QWidget(this);
		rightPanel->setFixedWidth(400);
		auto* rightLayout = new QVBoxLayout(rightPanel);
		rightLayout->setContentsMargins(0, 0, 0, 0);

		auto* scroll = new QScrollArea(this);
		scroll->setWidgetResizable(true);
		// 禁止左右滚动: 关闭水平滚动条, 内容严格贴合视口宽度, 不左右出界
		scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		scroll->setFrameShape(QFrame::NoFrame);
		// 右侧与左竖条统一用 Base 底色(比 Window 浅, 显式指定不依赖透明)
		scroll->viewport()->setAutoFillBackground(true);
		scroll->viewport()->setBackgroundRole(QPalette::Base);
		auto* content = new QWidget;
		content->setAutoFillBackground(true);
		content->setBackgroundRole(QPalette::Base);
		auto* layout = new QVBoxLayout(content);
		layout->setSpacing(10);

		// 皮肤(动画上方)
		auto* skinBox = new QGroupBox(VI18N("i18n:YSSSpineViewer::skin.title"), content);
		auto* skinLayout = new QVBoxLayout(skinBox);
		d->SkinGrid = new QGridLayout;
		d->SkinGrid->setSpacing(6);
		skinLayout->addLayout(d->SkinGrid);
		layout->addWidget(skinBox);

		// 动画: 上半"已应用到轨道"(BorderFrame 圆角边框, 表头 动画名|循环|层级, 每行 = [名字|循环|↑|↓], 双击行移回)
		//       + 下半"选择动画"(BorderFrame 圆角边框, 2 列 PushButton 网格, 点击添加)
		auto* animBox = new QGroupBox(VI18N("i18n:YSSSpineViewer::animation.title"), content);
		auto* animLayout = new QVBoxLayout(animBox);

		// 表头(三列): 动画名(伸缩) | 循环(28px) | 层级(56px = 两个 26px 按钮 + 间距)
		auto* appliedHeader = new QWidget(animBox);
		auto* headerLay = new QHBoxLayout(appliedHeader);
		headerLay->setContentsMargins(2, 0, 2, 0);
		headerLay->setSpacing(4);
		auto* hName = new QLabel(VI18N("i18n:YSSSpineViewer::animation.header.name"), appliedHeader);
		auto* hLoop = new QLabel(VI18N("i18n:YSSSpineViewer::animation.header.loop"), appliedHeader);
		hLoop->setFixedWidth(28);
		hLoop->setAlignment(Qt::AlignCenter);
		auto* hLevel = new QLabel(VI18N("i18n:YSSSpineViewer::animation.header.level"), appliedHeader);
		hLevel->setFixedWidth(56);
		hLevel->setAlignment(Qt::AlignCenter);
		headerLay->addWidget(hName, 1);
		headerLay->addWidget(hLoop);
		headerLay->addWidget(hLevel);
		animLayout->addWidget(appliedHeader);

		// 上半容器: BorderFrame 自动用 QStyle 画圆角边框; 每行定高, 容器高度按轨道数计算
		d->AppliedContainer = new Visindigo::Widgets::BorderFrame(animBox);
		d->AppliedRowsLayout = new QVBoxLayout(d->AppliedContainer);
		d->AppliedRowsLayout->setContentsMargins(6, 4, 6, 4);
		d->AppliedRowsLayout->setSpacing(2);
		animLayout->addWidget(d->AppliedContainer);
		updateAppliedContainerSize();

		// 下半容器: 圆角边框 + 内部滚动 + 2 列按钮网格
		auto* availableLabel = new QLabel(VI18N("i18n:YSSSpineViewer::animation.availableTitle"), animBox);
		d->AvailableContainer = new Visindigo::Widgets::BorderFrame(animBox);
		auto* availLay = new QVBoxLayout(d->AvailableContainer);
		availLay->setContentsMargins(6, 4, 6, 4);
		d->AvailableScroll = new QScrollArea(d->AvailableContainer);
		d->AvailableScroll->setFrameShape(QFrame::NoFrame);
		d->AvailableScroll->setWidgetResizable(true);
		d->AvailableScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		d->AvailableScroll->viewport()->setAutoFillBackground(false);
		d->AvailableScroll->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));
		d->AvailableScroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
		availLay->addWidget(d->AvailableScroll);
		d->AvailableGridHost = new QWidget(d->AvailableScroll);
		d->AvailableGridHost->setAutoFillBackground(false);
		d->AvailableGrid = new QGridLayout(d->AvailableGridHost);
		d->AvailableGrid->setContentsMargins(0, 0, 0, 0);
		d->AvailableGrid->setSpacing(4);
		d->AvailableScroll->setWidget(d->AvailableGridHost);
		d->AvailableContainer->setMaximumHeight(220);
		d->AvailableContainer->setMinimumHeight(90);
		animLayout->addWidget(availableLabel);
		animLayout->addWidget(d->AvailableContainer);

		layout->addWidget(animBox);

		// 属性
		auto* propBox = new QGroupBox(VI18N("i18n:YSSSpineViewer::properties.title"), content);
		auto* propLayout = new QVBoxLayout(propBox);
		auto addPropRow = [&](const QString& name, QSlider* slider, QLabel* valLabel) {
			auto* row = new QHBoxLayout;
			auto* lbl = new QLabel(name, propBox);
			lbl->setFixedWidth(120);
			row->addWidget(lbl);
			row->addWidget(slider, 1);
			row->addWidget(valLabel);
			propLayout->addLayout(row);
			};

		QLabel* fpsVal = new QLabel(QStringLiteral("60"), propBox);
		QSlider* fpsSlider = makeSlider(1, 240, 60);
		connect(fpsSlider, &QSlider::valueChanged, this, [this, fpsVal](int v) { d->Spine->setFps(v); fpsVal->setText(QString::number(v)); });
		addPropRow(VI18N("i18n:YSSSpineViewer::properties.fps"), fpsSlider, fpsVal);

		QLabel* scaleVal = new QLabel(QStringLiteral("0.50"), propBox);
		QSlider* scaleSlider = makeSlider(10, 200, 50);
		connect(scaleSlider, &QSlider::valueChanged, this, [this, scaleVal](int v) {
			if (d->UiSyncing) return;
			d->Spine->setSkeletonScale(v / 100.0);
			scaleVal->setText(QString::number(v / 100.0, 'f', 2));
			});
		addPropRow(VI18N("i18n:YSSSpineViewer::properties.scale"), scaleSlider, scaleVal);
		d->Spine->setSkeletonScale(0.5);   // 默认缩放 0.5(滑块初始 50)
		// 滚轮缩放 -> 缩放滑块同步(交互产生的 skeletonScale 变化)
		connect(d->Spine, &Visindigo::Widgets::SpineWidget::skeletonScaleChanged, this, [this, scaleSlider, scaleVal](qreal v) {
			if (d->UiSyncing) return;
			d->UiSyncing = true;
			scaleSlider->setValue(qRound(v * 100.0));
			scaleVal->setText(QString::number(v, 'f', 2));
			d->UiSyncing = false;
			});

		QLabel* timeVal = new QLabel(QStringLiteral("1.00"), propBox);
		QSlider* timeSlider = makeSlider(10, 200, 100);
		connect(timeSlider, &QSlider::valueChanged, this, [this, timeVal](int v) { d->Spine->setTimeScale(v / 100.0); timeVal->setText(QString::number(v / 100.0, 'f', 2)); });
		addPropRow(VI18N("i18n:YSSSpineViewer::properties.timeScale"), timeSlider, timeVal);

		QLabel* xVal = new QLabel(QStringLiteral("1.00"), propBox);
		QSlider* xSlider = makeSlider(-200, 200, 100);
		connect(xSlider, &QSlider::valueChanged, this, [this, xVal](int v) { d->Spine->setScaleX(v / 100.0); xVal->setText(QString::number(v / 100.0, 'f', 2)); });
		addPropRow(VI18N("i18n:YSSSpineViewer::properties.scaleX"), xSlider, xVal);

		QLabel* yVal = new QLabel(QStringLiteral("1.00"), propBox);
		QSlider* ySlider = makeSlider(-200, 200, 100);
		connect(ySlider, &QSlider::valueChanged, this, [this, yVal](int v) { d->Spine->setScaleY(v / 100.0); yVal->setText(QString::number(v / 100.0, 'f', 2)); });
		addPropRow(VI18N("i18n:YSSSpineViewer::properties.scaleY"), ySlider, yVal);

		QLabel* anchorXVal = new QLabel(QStringLiteral("0.00"), propBox);
		QSlider* anchorXSlider = makeSlider(-500, 500, 0);   // XY 偏移范围已解除 -1~1, 现为 -5~5
		connect(anchorXSlider, &QSlider::valueChanged, this, [this, anchorXVal](int v) {
			if (d->UiSyncing) return;
			d->Spine->setAnchorOffset(QPointF(v / 100.0, d->Spine->anchorOffset().y()));
			anchorXVal->setText(QString::number(v / 100.0, 'f', 2));
			});
		addPropRow(VI18N("i18n:YSSSpineViewer::properties.anchorX"), anchorXSlider, anchorXVal);

		QLabel* anchorYVal = new QLabel(QStringLiteral("1.00"), propBox);
		QSlider* anchorYSlider = makeSlider(-500, 500, 100);   // 初始 1.0, 范围 -5~5
		connect(anchorYSlider, &QSlider::valueChanged, this, [this, anchorYVal](int v) {
			if (d->UiSyncing) return;
			d->Spine->setAnchorOffset(QPointF(d->Spine->anchorOffset().x(), v / 100.0));
			anchorYVal->setText(QString::number(v / 100.0, 'f', 2));
			});
		addPropRow(VI18N("i18n:YSSSpineViewer::properties.anchorY"), anchorYSlider, anchorYVal);
		d->Spine->setAnchorOffset(QPointF(d->Spine->anchorOffset().x(), 1.0));   // 默认 Y 偏移 1.0(滑块初始 100)
		// 拖动/滚轮产生的 anchorOffset 变化 -> XY 滑块同步
		connect(d->Spine, &Visindigo::Widgets::SpineWidget::anchorOffsetChanged, this, [this, anchorXSlider, anchorXVal, anchorYSlider, anchorYVal](const QPointF& off) {
			if (d->UiSyncing) return;
			d->UiSyncing = true;
			anchorXSlider->setValue(qRound(off.x() * 100.0));
			anchorXVal->setText(QString::number(off.x(), 'f', 2));
			anchorYSlider->setValue(qRound(off.y() * 100.0));
			anchorYVal->setText(QString::number(off.y(), 'f', 2));
			d->UiSyncing = false;
			});

		QLabel* mixVal = new QLabel(QStringLiteral("0.20"), propBox);
		QSlider* mixSlider = makeSlider(0, 200, 20);
		connect(mixSlider, &QSlider::valueChanged, this, [this, mixVal](int v) { d->Spine->setDefaultMix(v / 100.0); mixVal->setText(QString::number(v / 100.0, 'f', 2)); });
		addPropRow(VI18N("i18n:YSSSpineViewer::properties.mix"), mixSlider, mixVal);
		layout->addWidget(propBox);

		layout->addStretch(1);
		scroll->setWidget(content);
		rightLayout->addWidget(scroll);

		// ---- 主布局: 左竖条 | 渲染区 | 右侧面板 ----
		auto* mainLayout = new QHBoxLayout(this);
		mainLayout->setContentsMargins(0, 0, 0, 0);
		mainLayout->setSpacing(0);
		mainLayout->addWidget(leftBar);
		mainLayout->addWidget(d->Spine, 1);
		mainLayout->addWidget(rightPanel);
		setLayout(mainLayout);

		connect(d->Spine, &Visindigo::Widgets::SpineWidget::resourceReady, this, &SpineViewerWidget::onResourceReady);
		connect(d->Spine, &Visindigo::Widgets::SpineWidget::resourceLoadFailed, this, &SpineViewerWidget::onLoadFailed);
		connect(d->Spine, &Visindigo::Widgets::SpineWidget::resourceReady, this, &SpineViewerWidget::updateStatus);
		// 注意: 不再连 animationCompleted -> updateStatus。库的 complete 事件在每个循环动画
		// 的每一圈都会触发, 会叠加到 500ms 定时器上让 FPS 刷新过快; 周期性刷新只由定时器负责。

		d->StatusTimer = new QTimer(this);
		d->StatusTimer->setInterval(500);
		connect(d->StatusTimer, &QTimer::timeout, this, &SpineViewerWidget::updateStatus);
		d->StatusTimer->start();
	}

	SpineViewerWidget::~SpineViewerWidget() {
		delete d;
	}

	QSlider* SpineViewerWidget::makeSlider(int from, int to, double value) {
		auto* slider = new QSlider(Qt::Horizontal, this);
		slider->setRange(from, to);
		slider->setValue(static_cast<int>(value));
		slider->setFixedWidth(190);
		return slider;
	}

	bool SpineViewerWidget::onOpen(const QString& filePath) {
		QFileInfo fi(filePath);
		const QString ext = fi.suffix().toLower();
		QString skelPath;
		QString atlasPath;
		if (ext == QStringLiteral("skel")) {
			skelPath = filePath;
			atlasPath = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".atlas");
		} else if (ext == QStringLiteral("atlas")) {
			atlasPath = filePath;
			skelPath = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".skel");
		} else {
			d->StatusLabel->setText(VI18N("i18n:YSSSpineViewer::status.unsupported").arg(filePath));
			return false;
		}
		if (!QFileInfo::exists(atlasPath)) {
			d->StatusLabel->setText(VI18N("i18n:YSSSpineViewer::status.noAtlas").arg(atlasPath));
			return false;
		}
		if (!QFileInfo::exists(skelPath)) {
			d->StatusLabel->setText(VI18N("i18n:YSSSpineViewer::status.noSkeleton").arg(skelPath));
			return false;
		}
		return loadSpineFiles(skelPath, atlasPath);
	}

	bool SpineViewerWidget::loadSpineFiles(const QString& skelPath, const QString& atlasPath) {
		d->StatusLabel->setText(VI18N("i18n:YSSSpineViewer::status.loading").arg(QFileInfo(skelPath).fileName()));
		d->Spine->setAtlasFile(atlasPath);
		d->Spine->setSkeletonFile(skelPath);
		d->Spine->reload();
		return true;
	}

	bool SpineViewerWidget::onClose() {
		d->Spine->clearTracks();
		return true;
	}

	bool SpineViewerWidget::onSave(const QString& filePath) {
		// 预览器只读, 无内容可保存
		Q_UNUSED(filePath)
		return true;
	}

	void SpineViewerWidget::onResourceReady() {
		rebuildAnimationAndSkinControls();
		// GPU 信息在窗口首次 GL 初始化后才就绪, 延迟刷新一次状态栏以便显示
		QTimer::singleShot(800, this, [this]() { updateStatus(); });
	}

	void SpineViewerWidget::onLoadFailed() {
		d->StatusLabel->setText(VI18N("i18n:YSSSpineViewer::status.loadFailed"));
	}

	void SpineViewerWidget::rebuildAnimationAndSkinControls() {
		// 清空旧的皮肤按钮
		auto clearGrid = [](QGridLayout* grid) {
			while (grid->count() > 0) {
				auto* item = grid->takeAt(0);
				if (auto* w = item->widget()) {
					grid->removeWidget(w);
					w->deleteLater();
				}
				delete item;
			}
			};
		clearGrid(d->SkinGrid);

		// 动画: 上半"已应用到轨道"=空; 下半"选择动画"=全部动画(按返回顺序, 点击按钮添加)。
		// 点击下半按钮 -> 移到上半末尾(最高轨道, 叠加); 双击上半行 -> 移回下半。
		// 上半行顺序 = 轨道号: 第0行 = track 0(基础), 越靠后轨道越高(越在上层叠加)。
		qDebug() << "SpineViewer animations:" << d->Spine->animations();
		// 清空已应用列表(逐行删除)
		while (d->AppliedRowsLayout->count() > 0) {
			auto* item = d->AppliedRowsLayout->takeAt(0);
			if (auto* w = item->widget())
				w->deleteLater();
			delete item;
		}
		d->Spine->clearTracks();
		rebuildAvailableGrid();   // 重建"选择动画"2 列按钮网格(全部动画 - 已应用)

		// 皮肤: 互斥 RadioButton(当前皮肤唯一)。初始选中与库侧 currentSkin() 一致
		// (库在 loadResource 自动应用附件最多的皮肤, 通常为角色主皮肤)。
		if (d->SkinGroup) { delete d->SkinGroup; d->SkinGroup = nullptr; }
		d->SkinGroup = new QButtonGroup(this);
		d->SkinGroup->setExclusive(true);
		const auto skins = d->Spine->skins();
		const QString current = d->Spine->currentSkin();
		const QString initialSkin = (not current.isEmpty() && skins.contains(current))
			? current : (skins.isEmpty() ? QString() : skins.first());
		int skinId = 0;
		int row = 0;
		for (const QString& name : skins) {
			auto* rb = new QRadioButton(name, this);
			d->SkinGroup->addButton(rb, skinId++);
			if (name == initialSkin)
				rb->setChecked(true);
			connect(rb, &QRadioButton::toggled, this, [this, name](bool on) {
				if (on)
					d->Spine->setSkin(name);
				});
			d->SkinGrid->addWidget(rb, row++, 0);
		}
	}

	/*! 点击"选择动画"按钮: 追加一行到"已应用到轨道"末尾(最高轨道, 叠加层)。 */
	void SpineViewerWidget::addToApplied(const QString& name) {
		if (name.isEmpty())
			return;
		d->AppliedRowsLayout->addWidget(makeAppliedRow(name, true));   // 追加 = 最高轨道叠加
		updateAppliedContainerSize();
		rebuildAvailableGrid();
		syncAppliedTracks();
	}

	/*! 双击"已应用"某行: 移回"选择动画"(重建网格, 按原始动画顺序恢复按钮)。 */
	void SpineViewerWidget::removeFromApplied(QWidget* row) {
		if (not row)
			return;
		const int idx = d->AppliedRowsLayout->indexOf(row);
		if (idx < 0)
			return;
		d->AppliedRowsLayout->takeAt(idx);
		row->deleteLater();
		updateAppliedContainerSize();
		rebuildAvailableGrid();
		syncAppliedTracks();
	}

	/*! 重建"选择动画"2 列按钮网格: 全部动画 - 已应用(按 Spine 返回顺序)。 */
	void SpineViewerWidget::rebuildAvailableGrid() {
		if (not d->AvailableGrid)
			return;
		// 收集已应用动画名
		QSet<QString> applied;
		for (int i = 0; i < d->AppliedRowsLayout->count(); ++i) {
			if (auto* row = d->AppliedRowsLayout->itemAt(i)->widget())
				applied.insert(row->property("animName").toString());
		}
		// 清空网格
		while (auto* item = d->AvailableGrid->takeAt(0)) {
			if (auto* w = item->widget())
				w->deleteLater();
			delete item;
		}
		// 按动画顺序重建(已应用跳过), 2 列排列
		const auto anims = d->Spine->animations();
		int idx = 0;
		for (const QString& name : anims) {
			if (applied.contains(name))
				continue;
			auto* btn = new QPushButton(name, d->AvailableGridHost);
			btn->setProperty("animName", name);
			connect(btn, &QPushButton::clicked, this, [this, name]() { addToApplied(name); });
			d->AvailableGrid->addWidget(btn, idx / 2, idx % 2);
			++idx;
		}
	}

	/*! 创建一行 [动画名 | 循环 | 上移 | 下移]。 */
	QWidget* SpineViewerWidget::makeAppliedRow(const QString& name, bool loop) {
		auto* row = new QWidget(d->AppliedContainer);
		row->setProperty("animName", name);
		row->setFixedHeight(kAppliedRowHeight);   // 固定行高, 避免被压缩挤在一起
		auto* h = new QHBoxLayout(row);
		h->setContentsMargins(0, 0, 0, 0);
		h->setSpacing(4);

		auto* nameLabel = new QLabel(name, row);
		auto* loopCb = new QCheckBox(row);
		loopCb->setChecked(loop);   // 默认循环
		loopCb->setFixedWidth(28);  // 与表头"循环"列对齐
		auto* upBtn = new QPushButton(QStringLiteral("↑"), row);
		auto* downBtn = new QPushButton(QStringLiteral("↓"), row);
		upBtn->setFixedWidth(26);
		downBtn->setFixedWidth(26);

		h->addWidget(nameLabel, 1);
		h->addWidget(loopCb);
		h->addWidget(upBtn);
		h->addWidget(downBtn);

		// 循环勾选 -> 重新设置该轨道
		connect(loopCb, &QCheckBox::toggled, this, [this, row](bool on) {
			const int idx = d->AppliedRowsLayout->indexOf(row);
			if (idx >= 0)
				d->Spine->setAnimation(idx, row->property("animName").toString(), on);
			});
		// 上移
		connect(upBtn, &QPushButton::clicked, this, [this, row]() {
			const int idx = d->AppliedRowsLayout->indexOf(row);
			if (idx > 0)
				moveAppliedRow(idx, idx - 1);
			});
		// 下移
		connect(downBtn, &QPushButton::clicked, this, [this, row]() {
			const int idx = d->AppliedRowsLayout->indexOf(row);
			if (idx >= 0 && idx < d->AppliedRowsLayout->count() - 1)
				moveAppliedRow(idx, idx + 1);
			});
		// 双击行(名字区域) -> 移回选择列表
		row->installEventFilter(this);
		return row;
	}

	/*! 在已应用布局内移动一行(from -> to), 并重新同步轨道。 */
	void SpineViewerWidget::moveAppliedRow(int from, int to) {
		if (from == to || from < 0 || to < 0 || from >= d->AppliedRowsLayout->count())
			return;
		QLayoutItem* item = d->AppliedRowsLayout->takeAt(from);
		if (item)
			d->AppliedRowsLayout->insertItem(to, item);
		syncAppliedTracks();
	}

	/*! 已应用容器高度 = 上下边距 + 行数×行高 + 行间距(至少 60px 保持空盒可见)。 */
	void SpineViewerWidget::updateAppliedContainerSize() {
		if (not d->AppliedContainer || not d->AppliedRowsLayout)
			return;
		const int n = d->AppliedRowsLayout->count();
		const QMargins m = d->AppliedRowsLayout->contentsMargins();
		const int spacing = d->AppliedRowsLayout->spacing();
		const int h = m.top() + m.bottom() + n * kAppliedRowHeight + (n > 0 ? (n - 1) * spacing : 0);
		d->AppliedContainer->setFixedHeight(qMax(h, 60));
	}

	/*! 双击已应用某行 -> 移回选择列表。 */
	bool SpineViewerWidget::eventFilter(QObject* obj, QEvent* ev) {
		if (ev->type() == QEvent::MouseButtonDblClick) {
			auto* w = qobject_cast<QWidget*>(obj);
			if (w && d->AppliedRowsLayout && d->AppliedRowsLayout->indexOf(w) >= 0) {
				removeFromApplied(w);
				return true;
			}
		}
		return QWidget::eventFilter(obj, ev);
	}

	/*! 按"已应用"行顺序同步轨道: 第 i 行 -> track i(0=基础, 越大越在上层叠加)。 */
	void SpineViewerWidget::syncAppliedTracks() {
		if (not d->Spine->isSkeletonReady() || d->Syncing)
			return;
		d->Syncing = true;
		d->Spine->clearTracks();
		for (int i = 0; i < d->AppliedRowsLayout->count(); ++i) {
			auto* row = d->AppliedRowsLayout->itemAt(i)->widget();
			if (not row)
				continue;
			QCheckBox* cb = row->findChild<QCheckBox*>();
			d->Spine->setAnimation(i, row->property("animName").toString(), cb ? cb->isChecked() : true);
		}
		d->Syncing = false;
	}

	void SpineViewerWidget::updateStatus() {
		if (!d->StatusLabel) {
			return;
		}
		if (d->Spine->isSkeletonReady()) {
			// 已加载内容(3 行): 动画 / 皮肤 / FPS(未开自适应读普通 fps, 开启读自适应 fps)
			const double fpsValue = d->Spine->isAutoFpsEnable() ? d->Spine->getAutoFps() : double(d->Spine->fps());
			const QString fpsText = VI18N("i18n:YSSSpineViewer::status.fps").arg(QString::number(fpsValue, 'f', 1));
			d->StatusLabel->setText(VI18N("i18n:YSSSpineViewer::status.loaded")
				.arg(d->Spine->animations().size())
				.arg(d->Spine->skins().size())
				.arg(fpsText));
			// GPU(左底部, 自动换行不超宽): GL 上下文首次初始化后才有值
			if (d->GpuLabel) {
				d->GpuLabel->setText(d->Spine->gpuVendor().isEmpty()
					? VI18N("i18n:YSSSpineViewer::status.gpuPending")
					: VI18N("i18n:YSSSpineViewer::status.gpuFormat").arg(d->Spine->gpuVendor(), d->Spine->gpuRenderer()));
			}
		}
	}
}
