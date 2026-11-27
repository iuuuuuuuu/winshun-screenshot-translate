#include "WindowSizeMemory.h"
#include "Services.h"
#include <QFile>
#include <QJsonDocument>
#include <QScreen>
#include <QGuiApplication>
#include <QDebug>
#include <algorithm>
#include <windows.h>

namespace st {
namespace {
QJsonObject windowSizes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly)?QJsonDocument::fromJson(file.readAll()).object():QJsonObject{};
}
}
void WindowSizeMemory::install(QQuickWindow* window,const QString& directory)
{
    const auto key=window->property("windowSizeKey").toString();
    if(!key.isEmpty())new WindowSizeMemory(window,directory+"/window-sizes.json",key);
}
WindowSizeMemory::WindowSizeMemory(QQuickWindow* window,QString file,QString key)
    :QObject(window),m_window(window),m_file(std::move(file)),m_key(std::move(key)),
      m_normal(window->size()),m_minimum(window->minimumSize())
{
    const auto saved=windowSizes(m_file).value(m_key).toObject();
    const auto width=saved.value("width").toInt(),height=saved.value("height").toInt();
    if(width>0&&height>0)m_normal=QSize(width,height);
    m_maximized=saved.value("maximized").toBool();
    m_timer.setSingleShot(true);m_timer.setInterval(250);
    auto resized=[this]{if(!m_restoring&&m_window->isVisible()){capture();m_timer.start();}};
    connect(window,&QWindow::widthChanged,this,resized);
    connect(window,&QWindow::heightChanged,this,resized);
    connect(&m_timer,&QTimer::timeout,this,[this]{capture();save();});
    connect(window,&QWindow::windowStateChanged,this,[this](Qt::WindowState state){
        if(m_restoring||!m_shown)return;
        // The maximized/minimized geometry must never replace the user's
        // normal size. A queued capture waits for native geometry to settle.
        if(state==Qt::WindowMaximized||state==Qt::WindowNoState){
            if(state==Qt::WindowMaximized){
                // QWindow resize notifications can still be queued when the
                // next action maximizes it. Windows retains the actual restore
                // rectangle even then; these frameless windows have no caption
                // or resize border to subtract from the client dimensions.
                WINDOWPLACEMENT placement{sizeof placement};
                if(GetWindowPlacement(HWND(m_window->winId()),&placement)){
                    const auto rect=placement.rcNormalPosition;const auto dpr=m_window->devicePixelRatio();
                    const QSize normal(qRound((rect.right-rect.left)/dpr),qRound((rect.bottom-rect.top)/dpr));
                    if(normal.isValid())m_normal=normal;
                }
            }
            m_maximized=state==Qt::WindowMaximized;m_dirty=true;m_timer.start();
        }
    });
    connect(window,&QWindow::visibleChanged,this,[this](bool visible){
        if(visible)m_shown=true;
        else if(m_shown&&!m_restoring){m_timer.stop();capture();save();}
    });
    connect(qGuiApp,&QCoreApplication::aboutToQuit,this,[this]{capture();save();});
}
void WindowSizeMemory::capture()
{
    if(m_restoring||!m_shown)return;
    if(m_window->windowState()==Qt::WindowNoState){
        const auto size=m_window->size();
        if(size.isValid()&&size!=m_normal){m_normal=size;m_dirty=true;}
    }
}
void WindowSizeMemory::save()
{
    if(!m_dirty)return;
    try{
        // Merge the current file so saving one window never replaces another
        // window's independently saved dimensions.
        auto saved=windowSizes(m_file);
        saved[m_key]=QJsonObject{{"width",m_normal.width()},{"height",m_normal.height()},{"maximized",m_maximized}};
        atomicJson(m_file,saved);m_dirty=false;
    }catch(const std::exception& e){qWarning()<<"Window size could not be saved:"<<e.what();}
}
void WindowSizeMemory::restore(QScreen* screen)
{
    m_restoring=true;
    m_window->setWindowState(Qt::WindowNoState);
    const auto area=screen->availableGeometry();m_window->setScreen(screen);
    const auto available=QSize(std::max(1,area.width()-24),std::max(1,area.height()-40));
    const auto minimum=m_minimum.boundedTo(available);
    m_window->setMinimumSize(minimum);m_window->setMaximumSize(area.size());
    m_window->resize(m_normal.expandedTo(minimum).boundedTo(available));
    m_window->setPosition(area.center()-QPoint(m_window->width()/2,m_window->height()/2));
    if(m_maximized)m_window->showMaximized();else m_window->showNormal();
    m_shown=true;m_restoring=false;
}
void WindowSizeMemory::show(QQuickWindow* window,QScreen* screen)
{
    auto memory=window->findChild<WindowSizeMemory*>();
    if(memory&&!window->isVisible())memory->restore(screen);
    else if(window->windowState()==Qt::WindowMinimized){
        if(memory&&memory->m_maximized)window->showMaximized();else window->showNormal();
    }
    else window->show();
    window->raise();window->requestActivate();
}
}
