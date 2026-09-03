#ifndef DEMO_H
#define DEMO_H

#include <QWidget>
#include <QLabel>

namespace Visindigo { namespace Widgets { class SpineWidget; } }
class QGridLayout;
class QVBoxLayout;
class QHBoxLayout;
class QCheckBox;
class QSlider;
class QButtonGroup;
class QTimer;
class QFrame;
class QEvent;
class QPushButton;
class QResizeEvent;
class QPoint;

/**
 * Demo: 重构为与 Plugin(YSSSpineViewer) 一致的 UI, 但全部使用 Qt 组件
 * (圆角容器用 QFrame + 样式表, 不用 Visindigo 的 BorderFrame)。
 * 保留 Demo 特有的无边框/点击穿透窗口特性。
 */
class DemoWindow : public QWidget
{
    Q_OBJECT
public:
    explicit DemoWindow(QWidget *parent = nullptr);

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onResourceReady();
    void onLoadFailed();
    void updateStatus();

private:
    QSlider *makeSlider(int from, int to, double value);
    QFrame *makeRoundedFrame(QWidget *parent);

    // 已应用轨道
    QWidget *makeAppliedRow(const QString &name, bool loop);
    void addToApplied(const QString &name);
    void removeFromApplied(QWidget *row);
    void moveAppliedRow(int from, int to);
    void rebuildAvailableGrid();
    void syncAppliedTracks();
    void updateAppliedContainerSize();

    // 无边框点击穿透: 定时器按光标位置动态开关 WS_EX_TRANSPARENT(仅 Spine 的透明像素
    // 穿透到桌面/下层窗口, 骨架本体与右侧面板保持可交互)。透明判定由
    // SpineWidget::hitTest() 按帧缓冲 alpha 逐像素完成。
    void startClickThrough();
    void stopClickThrough();
    void updateClickThrough();
    // 无边框悬浮模式: 面板覆盖在整页 Spine 上, 可各自收起/展开
    void setFloatingMode(bool floating);
    void relayoutFloating();
    bool overInteractiveUi(const QPoint &globalPos);

    Visindigo::Widgets::SpineWidget *m_spine;
    QGridLayout *m_skinGrid = nullptr;
    QButtonGroup *m_skinGroup = nullptr;
    QFrame *m_appliedContainer = nullptr;   // 已应用容器(纯 QFrame 圆角)
    QVBoxLayout *m_appliedRows = nullptr;
    QFrame *m_availableContainer = nullptr; // 选择动画容器(纯 QFrame 圆角)
    QWidget *m_availableHost = nullptr;
    QGridLayout *m_availableGrid = nullptr;
    QLabel *m_statusLabel = nullptr;        // 已加载内容(左竖条 3 行)
    QLabel *m_gpuLabel = nullptr;           // GPU(左竖条底部)
    QCheckBox *m_debugBones = nullptr;
    QCheckBox *m_debugSlots = nullptr;
    QCheckBox *m_debugMesh = nullptr;
    bool m_frameless = false;               // 当前是否为无边框(用于点击穿透)
    bool m_uiSyncing = false;               // UI 滑块与库信号互同步防回环
    QTimer *m_clickThroughTimer = nullptr;
    QTimer *m_statusTimer = nullptr;
    // 无边框悬浮模式
    QHBoxLayout *m_mainLayout = nullptr;
    QWidget *m_leftBar = nullptr;
    QWidget *m_rightPanel = nullptr;
    QPushButton *m_leftCollapseBtn = nullptr;  // 左栏顶部"收起"
    QPushButton *m_rightCollapseBtn = nullptr; // 右栏顶部"收起"
    QPushButton *m_leftTab = nullptr;          // 左栏收起后浮在边缘的展开柄
    QPushButton *m_rightTab = nullptr;
    bool m_leftVisible = true;
    bool m_rightVisible = true;
    bool m_floating = false;                   // 当前是否无边框悬浮
    QColor m_bgBeforeFrameless;                // 进入无边框前的背景色
};

#endif // DEMO_H
