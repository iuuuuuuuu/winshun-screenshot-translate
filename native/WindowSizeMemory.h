#pragma once
#include <QObject>
#include <QQuickWindow>
#include <QTimer>

namespace st {
// Utility windows opt in with a distinct windowSizeKey QML property.
// Adaptive translation popups deliberately do not opt in.
class WindowSizeMemory : public QObject {
    Q_OBJECT
public:
    static void install(QQuickWindow* window,const QString& directory);
    static void show(QQuickWindow* window,QScreen* screen);
private:
    WindowSizeMemory(QQuickWindow* window,QString file,QString key);
    void capture();
    void save();
    void restore(QScreen* screen);
    QQuickWindow* m_window;
    QString m_file,m_key;
    QSize m_normal,m_minimum;
    bool m_maximized=false,m_restoring=false,m_shown=false,m_dirty=false;
    QTimer m_timer;
};
}
