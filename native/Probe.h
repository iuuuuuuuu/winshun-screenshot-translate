#pragma once
#include <QJsonObject>
namespace st {
class App;
class Probe {
public:
    static bool privateDesktop();
    static bool requested();
    static void enable();
    static void run(App& app,const QJsonObject& params);
};
}
