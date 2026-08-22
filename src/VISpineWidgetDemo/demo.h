#ifndef DEMO_H
#define DEMO_H

#include <QWidget>
#include <QLabel>

namespace Visindigo { namespace Widgets { class SpineWidget; } }
class QGridLayout;
class QCheckBox;
class QSlider;
class QButtonGroup;
class QTimer;

/**
 * Demo: a normal QtWidgets window with a transparent-background SpineWidget on
 * the left and a plain-widget control panel on the right. Shows that Spine can
 * be embedded into an ordinary widgets UI without any QML runtime.
 */
class DemoWindow : public QWidget
{
    Q_OBJECT
public:
    explicit DemoWindow(QWidget *parent = nullptr);

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private slots:
    void onResourceReady();
    void onLoadFailed();
    void updateStatus();

private:
    QSlider *makeSlider(int from, int to, double value, QLabel **valueLabel);

    // 无边框点击穿透: 定时器按光标位置动态开关 WS_EX_TRANSPARENT(仅 Spine 的透明像素
    // 穿透到桌面/下层窗口, 骨架本体与右侧面板保持可交互)。透明判定由
    // SpineWidget::hitTest() 按帧缓冲 alpha 逐像素完成, 与渲染内容一致。
    // HTTRANSPARENT 只对同线程下层窗口生效, 跨进程/桌面必须用 WS_EX_TRANSPARENT
    // (整窗属性, 故需轮询光标实现局部穿透)。
    void startClickThrough();
    void stopClickThrough();
    void updateClickThrough();

    Visindigo::Widgets::SpineWidget *m_spine;
    QGridLayout *m_animGrid = nullptr;
    QGridLayout *m_skinGrid = nullptr;
    QButtonGroup *m_skinGroup = nullptr;   // 皮肤互斥组(当前皮肤唯一)
    bool m_frameless = false;              // 当前是否为无边框(用于点击穿透)
    QTimer *m_clickThroughTimer = nullptr; // 穿透状态轮询(WS_EX_TRANSPARENT 动态开关)
    QTimer *m_statusTimer = nullptr;       // 状态栏定期刷新(动态数据如 AutoFPS)
    QLabel *m_statusLabel = nullptr;
    QCheckBox *m_debugBones = nullptr;
    QCheckBox *m_debugSlots = nullptr;
    QCheckBox *m_debugMesh = nullptr;
};

#endif // DEMO_H
