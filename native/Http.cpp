// Async handle lifetime and request wait adapted from LingCore/WinShun
// src/translate-plugin/Http.cpp, ef57fb8, MIT (vendor/LICENSE).
#include <windows.h>
#include <winhttp.h>
#include "Http.h"
#include <QUrl>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
namespace st {
namespace {
class Request {
public:
    explicit Request(Cancel canceled):m_canceled(std::move(canceled)){}
    ~Request()
    {
        if(handle){
            WinHttpCloseHandle(handle);
            if(callbacks){std::unique_lock lock(m_mutex);m_cv.wait(lock,[this]{return m_closed;});}
        }
        if(connection)WinHttpCloseHandle(connection);
    }
    static void CALLBACK status(HINTERNET,DWORD_PTR context,DWORD status,void* info,DWORD size)
    {
        if(!context)return;
        auto& r=*reinterpret_cast<Request*>(context);std::lock_guard lock(r.m_mutex);
        switch(status){
        case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:r.m_closed=true;break;
        case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
            r.m_error=info&&size>=sizeof(WINHTTP_ASYNC_RESULT)?static_cast<WINHTTP_ASYNC_RESULT*>(info)->dwError:ERROR_WINHTTP_INTERNAL_ERROR;break;
        case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
            r.bytes=info&&size>=sizeof(DWORD)?*static_cast<DWORD*>(info):0;r.m_completed=status;break;
        case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:r.bytes=size;r.m_completed=status;break;
        case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
        case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:r.m_completed=status;break;
        default:return;
        }
        r.m_cv.notify_all();
    }
    void prepare(){std::lock_guard lock(m_mutex);m_completed=0;bytes=0;}
    DWORD wait(BOOL started,DWORD expected,std::chrono::steady_clock::time_point deadline)
    {
        if(!started){auto error=GetLastError();if(error!=ERROR_IO_PENDING)return error;}
        std::unique_lock lock(m_mutex);
        while(!m_error&&m_completed!=expected){
            if(m_canceled->load())return ERROR_CANCELLED;
            auto now=std::chrono::steady_clock::now();if(now>=deadline)return ERROR_WINHTTP_TIMEOUT;
            m_cv.wait_until(lock,std::min(deadline,now+std::chrono::milliseconds(25)));
        }
        return m_canceled->load()?ERROR_CANCELLED:m_error;
    }
    HINTERNET connection=nullptr,handle=nullptr;
    bool callbacks=false;
    DWORD bytes=0;
    std::array<char,64*1024> buffer;
private:
    Cancel m_canceled;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    DWORD m_completed=0,m_error=0;
    bool m_closed=false;
};
HINTERNET session(const QString& proxy)
{
    // Sessions are shared for connection pooling, and kept until process exit.
    // AUTOMATIC_PROXY uses Windows' per-URL PAC and certificate store.
    static std::mutex mutex;static std::map<QString,HINTERNET> sessions;
    std::lock_guard lock(mutex);
    if(auto found=sessions.find(proxy);found!=sessions.end())return found->second;
    auto handle=WinHttpOpen(L"WinShun-screenshot-translate/0.4.1",
        proxy.isEmpty()?WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY:WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC);
    if(handle)sessions.emplace(proxy,handle);
    return handle;
}
}
HttpResponse nativeRequest(const QString& url,const QByteArray& body,bool post,
    const QMap<QByteArray,QByteArray>& headers,const QString& proxy,Cancel cancel,int budget)
{
    HttpResponse response;
    auto failed=[&](DWORD error){response.error=error;return response;};
    if(cancel->load())return failed(ERROR_CANCELLED);
    if(budget<=0)return failed(ERROR_WINHTTP_TIMEOUT);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(budget);
    auto uri=QUrl(url).toEncoded();auto wide=QString::fromLatin1(uri).toStdWString();
    URL_COMPONENTS parts{};parts.dwStructSize=sizeof parts;
    parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=DWORD(-1);
    if(!WinHttpCrackUrl(wide.c_str(),0,0,&parts))return failed(GetLastError());
    auto shared=session(proxy);if(!shared)return failed(GetLastError());
    Request request(cancel);
    std::wstring host(parts.lpszHostName,parts.dwHostNameLength);
    request.connection=WinHttpConnect(shared,host.c_str(),parts.nPort,0);
    if(!request.connection)return failed(GetLastError());
    std::wstring path(parts.lpszUrlPath,parts.dwUrlPathLength);if(path.empty())path=L"/";
    if(parts.dwExtraInfoLength)path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
    request.handle=WinHttpOpenRequest(request.connection,post?L"POST":L"GET",path.c_str(),nullptr,
        WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,parts.nScheme==INTERNET_SCHEME_HTTPS?WINHTTP_FLAG_SECURE:0);
    if(!request.handle)return failed(GetLastError());
    if(!proxy.isEmpty()){
        QUrl address(proxy);auto proxyHost=address.host().toStdWString();
        WINHTTP_PROXY_RESULT_ENTRY entry{TRUE,FALSE,
            address.scheme()=="https"?INTERNET_SCHEME_HTTPS:INTERNET_SCHEME_HTTP,
            proxyHost.data(),INTERNET_PORT(address.port(address.scheme()=="https"?443:80))};
        if(!WinHttpSetOption(request.handle,WINHTTP_OPTION_PROXY_RESULT_ENTRY,&entry,sizeof entry))return failed(GetLastError());
    }
    DWORD_PTR context=reinterpret_cast<DWORD_PTR>(&request);
    if(!WinHttpSetOption(request.handle,WINHTTP_OPTION_CONTEXT_VALUE,&context,sizeof context))return failed(GetLastError());
    if(WinHttpSetStatusCallback(request.handle,&Request::status,
        WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS|WINHTTP_CALLBACK_FLAG_HANDLES,0)==WINHTTP_INVALID_STATUS_CALLBACK)return failed(GetLastError());
    request.callbacks=true;
    DWORD disable=WINHTTP_DISABLE_REDIRECTS|WINHTTP_DISABLE_COOKIES;
    if(!WinHttpSetOption(request.handle,WINHTTP_OPTION_DISABLE_FEATURE,&disable,sizeof disable))return failed(GetLastError());
    WinHttpSetTimeouts(request.handle,budget,budget,budget,budget);
    DWORD compression=WINHTTP_DECOMPRESSION_FLAG_GZIP|WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
    WinHttpSetOption(request.handle,WINHTTP_OPTION_DECOMPRESSION,&compression,sizeof compression);
    std::wstring text;
    for(auto it=headers.begin();it!=headers.end();++it){
        if(it.key().contains('\r')||it.key().contains('\n')||it.key().contains(':')||
            it.value().contains('\r')||it.value().contains('\n'))return failed(ERROR_INVALID_PARAMETER);
        text+=QString::fromLatin1(it.key()).toStdWString()+L": "+QString::fromUtf8(it.value()).toStdWString()+L"\r\n";
    }
    auto error=request.wait(WinHttpSendRequest(request.handle,text.c_str(),DWORD(text.size()),
        body.isEmpty()?WINHTTP_NO_REQUEST_DATA:const_cast<char*>(body.constData()),DWORD(body.size()),DWORD(body.size()),context),
        WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE,deadline);
    if(error)return failed(error);
    request.prepare();
    error=request.wait(WinHttpReceiveResponse(request.handle,nullptr),WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE,deadline);
    if(error)return failed(error);
    DWORD size=sizeof response.status;
    if(!WinHttpQueryHeaders(request.handle,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,&response.status,&size,WINHTTP_NO_HEADER_INDEX))return failed(GetLastError());
    if(response.status>=300&&response.status<400){
        DWORD length=0;
        WinHttpQueryHeaders(request.handle,WINHTTP_QUERY_LOCATION,WINHTTP_HEADER_NAME_BY_INDEX,nullptr,&length,WINHTTP_NO_HEADER_INDEX);
        if(length){
            std::wstring location(length/sizeof(wchar_t),L'\0');
            if(WinHttpQueryHeaders(request.handle,WINHTTP_QUERY_LOCATION,WINHTTP_HEADER_NAME_BY_INDEX,
                location.data(),&length,WINHTTP_NO_HEADER_INDEX))response.location=QString::fromWCharArray(location.c_str());
        }
        return response;
    }
    for(;;){
        request.prepare();
        error=request.wait(WinHttpQueryDataAvailable(request.handle,nullptr),WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE,deadline);
        if(error)return failed(error);
        if(!request.bytes)break;
        if(request.bytes>8*1024*1024-response.body.size())return failed(ERROR_FILE_TOO_LARGE);
        auto count=std::min(request.bytes,DWORD(request.buffer.size()));request.prepare();
        error=request.wait(WinHttpReadData(request.handle,request.buffer.data(),count,nullptr),WINHTTP_CALLBACK_STATUS_READ_COMPLETE,deadline);
        if(error)return failed(error);
        response.body.append(request.buffer.data(),request.bytes);if(!request.bytes)break;
    }
    return response;
}
}
