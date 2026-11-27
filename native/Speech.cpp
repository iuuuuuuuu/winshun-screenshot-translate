#include "Speech.h"
#include <windows.h>
#include <sapi.h>
#include <wrl/client.h>
#include <QMap>
#include <QSet>
namespace st {
namespace {
using Microsoft::WRL::ComPtr;
const QMap<QString,WORD> languages{{"en",LANG_ENGLISH},{"zh",LANG_CHINESE},{"ja",LANG_JAPANESE},{"ko",LANG_KOREAN},
    {"fr",LANG_FRENCH},{"de",LANG_GERMAN},{"es",LANG_SPANISH},{"ru",LANG_RUSSIAN}};
struct Com { HRESULT result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);~Com(){if(SUCCEEDED(result))CoUninitialize();} };
QList<QPair<ComPtr<ISpObjectToken>,QSet<WORD>>> voices()
{
    QList<QPair<ComPtr<ISpObjectToken>,QSet<WORD>>> result;ComPtr<ISpObjectTokenCategory> category;
    if(FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&category)))||FAILED(category->SetId(SPCAT_VOICES,FALSE)))return result;
    ComPtr<IEnumSpObjectTokens> tokens;if(FAILED(category->EnumTokens(nullptr,nullptr,&tokens)))return result;
    ULONG count=0;if(FAILED(tokens->GetCount(&count)))return result;
    for(ULONG i=0;i<count;++i){
        ComPtr<ISpObjectToken> token;ComPtr<ISpDataKey> attributes;wchar_t* value=nullptr;
        if(FAILED(tokens->Item(i,&token))||FAILED(token->OpenKey(L"Attributes",&attributes))||FAILED(attributes->GetStringValue(L"Language",&value)))continue;
        auto text=QString::fromWCharArray(value);CoTaskMemFree(value);QSet<WORD> codes;
        for(auto part:text.split(';')){bool ok=false;auto code=part.toUInt(&ok,16);if(ok)codes.insert(PRIMARYLANGID(code));}
        result<<qMakePair(token,codes);
    }return result;
}
}
Speech::Speech(bool silent):m_silent(silent),m_thread([this](std::stop_token stop){run(stop);}){}
Speech::~Speech(){m_thread.request_stop();m_changed.notify_all();if(m_thread.joinable())m_thread.join();}
void Speech::say(QString text,QString language,Callback callback)
{
    std::lock_guard lock(m_mutex);m_text=std::move(text);m_language=std::move(language);m_callback=std::move(callback);++m_revision;m_changed.notify_all();
}
void Speech::stop(){std::lock_guard lock(m_mutex);m_text.clear();++m_revision;m_changed.notify_all();}
QStringList Speech::languages()
{
    Com com;QStringList result;if(FAILED(com.result)&&com.result!=RPC_E_CHANGED_MODE)return result;
    for(const auto& voice:voices())for(auto it=st::languages.begin();it!=st::languages.end();++it)if(voice.second.contains(it.value())&&!result.contains(it.key()))result<<it.key();
    return result;
}
void Speech::run(std::stop_token stop)
{
    Com com;quint64 previous=0;
    while(!stop.stop_requested()){
        QString text,language;Callback callback;quint64 revision;
        {
            std::unique_lock lock(m_mutex);m_changed.wait(lock,[&]{return stop.stop_requested()||m_revision!=previous;});
            if(stop.stop_requested())return;revision=previous=m_revision;text=m_text;language=m_language;callback=m_callback;
        }
        auto notify=[&](bool active,const QString& error={}){if(callback)callback(active,error);};
        if(text.trimmed().isEmpty()){notify(false);continue;}
        if(FAILED(com.result)){notify(false,"无法启动 Windows 朗读服务");continue;}
        ComPtr<ISpVoice> voice;
        if(FAILED(CoCreateInstance(CLSID_SpVoice,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&voice)))){notify(false,"Windows 朗读服务不可用");continue;}
        ComPtr<ISpObjectToken> selected;
        auto code=st::languages.value(language);
        for(const auto& entry:voices())if(code&&entry.second.contains(code)){selected=entry.first;break;}
        if(!selected){notify(false,"Windows 尚未安装该语言的语音，请在系统语言设置中安装");continue;}
        if(FAILED(voice->SetVoice(selected.Get()))){notify(false,"无法使用所选 Windows 语音");continue;}
        ComPtr<ISpStream> audio;ComPtr<IStream> memory;
        if(m_silent){
            WAVEFORMATEX format{WAVE_FORMAT_PCM,1,22050,44100,2,16,0};
            if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&memory))||FAILED(CoCreateInstance(CLSID_SpStream,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&audio)))||
                FAILED(audio->SetBaseStream(memory.Get(),SPDFID_WaveFormatEx,&format))||FAILED(voice->SetOutput(audio.Get(),TRUE))){notify(false,"无法初始化静音验证输出");continue;}
        }
        auto wide=text.toStdWString();
        if(FAILED(voice->Speak(wide.c_str(),SPF_ASYNC|SPF_PURGEBEFORESPEAK|SPF_IS_NOT_XML,nullptr))){notify(false,"朗读失败，请检查 Windows 语音设置");continue;}
        notify(true);
        bool interrupted=false;
        for(;;){
            if(stop.stop_requested()){interrupted=true;break;}
            {std::lock_guard lock(m_mutex);if(revision!=m_revision){interrupted=true;break;}}
            const auto done=voice->WaitUntilDone(40);if(done==S_OK)break;
            if(FAILED(done)){notify(false,"朗读过程中发生错误");interrupted=true;break;}
        }
        if(interrupted)voice->Speak(nullptr,SPF_ASYNC|SPF_PURGEBEFORESPEAK,nullptr);
        notify(false);
    }
}
}
