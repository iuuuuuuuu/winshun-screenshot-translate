#pragma once
#include <windows.h>
#include "Services.h"
#include <QMap>
#include <QByteArray>
namespace st {
struct HttpResponse {
    DWORD status=0,error=0;
    QByteArray body;
    QString location;
};
HttpResponse nativeRequest(const QString& url,const QByteArray& body,bool post,
    const QMap<QByteArray,QByteArray>& headers,const QString& proxy,Cancel cancel,int budget);
}
