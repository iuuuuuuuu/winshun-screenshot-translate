#pragma once
#include "Services.h"
#include <QImage>
#include <QRect>
#include <QList>
#include <windows.h>
#include <functional>
#include <thread>
#include <vector>
namespace st {
struct Block { QString text; QRectF box; double glyphHeight=16; };
QList<Block> groupWords(QList<Block> words);
QList<Block> recognize(const QImage& image, Cancel cancel);
QImage capture(const QRect& area);
QRect virtualScreen();
QRect monitorRect(POINT point, bool work = false);
QString selectedText();
QImage translatedImage(const QImage& source, const QList<Block>& blocks, const QStringList& segments, bool dark);
QImage textImage(const QString& text, int width, bool dark);
QImage comparisonImage(const QImage& source,const QImage& translated,bool translationFirst,bool dark,bool chinese=true);
class Selector {
public:
    using Done=std::function<void(QImage,QRect)>;
    explicit Selector(Done done,std::function<QImage(const QRect&)> grab=capture);
    ~Selector();
    bool show();
    void cancel();
    const std::vector<HWND>& windows() const { return m_windows; }
    void input(UINT message,POINT point); // same physical-coordinate path as native messages
private:
    struct Surface { HWND hwnd=nullptr; QRect rect; QImage image,dim; Selector* owner=nullptr; };
    static LRESULT CALLBACK proc(HWND,UINT,WPARAM,LPARAM);
    void finish(QRect rect);
    Done m_done;
    std::function<QImage(const QRect&)> m_grab;
    std::vector<std::unique_ptr<Surface>> m_surfaces;
    std::vector<HWND> m_windows;
    QPoint m_start;
    QRect m_selection;
    bool m_drag=false,m_closed=false;
};
class RawClicks {
    friend class Probe;
public:
    explicit RawClicks(std::function<void(POINT,DWORD)> callback);
    ~RawClicks();
private:
    static LRESULT CALLBACK proc(HWND,UINT,WPARAM,LPARAM);
    std::function<void(POINT,DWORD)> m_callback;
    std::thread m_thread;
    std::atomic<DWORD> m_id{0};
    std::atomic<HWND> m_hwnd{nullptr};
    std::atomic_bool m_stop{false};
    std::atomic<decltype(&::GetRawInputData)> m_read{&::GetRawInputData};
    std::atomic<decltype(&::GetPhysicalCursorPos)> m_cursor{&::GetPhysicalCursorPos};
};
}
