#include "AiStream.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <stdexcept>
namespace st {
namespace {
[[noreturn]] void invalid(const char* message){throw std::runtime_error(message);}
QString answer(QString value)
{
    value=value.trimmed();
    // Reasoning tags are accepted only before the answer. Literal tags inside
    // a translated JSON string must remain untouched.
    while(value.startsWith("<think>",Qt::CaseInsensitive)){
        auto end=value.indexOf("</think>",0,Qt::CaseInsensitive);if(end<0)return {};
        value=value.mid(end+8).trimmed();
    }
    if(value.startsWith("```")){
        auto end=value.indexOf('\n');if(end<0)return {};value=value.mid(end+1).trimmed();
        if(value.endsWith("```"))value.chop(3);
    }
    return value.trimmed();
}
void reason(const QString& value)
{
    if(value=="length")invalid("AI 输出被截断，请增大服务输出上限或切换模型");
    if(value=="content_filter")invalid("AI 服务未能完成这段翻译");
    if(!value.isEmpty()&&value!="stop")invalid("AI 未返回完整译文");
}
}
QStringList aiArrayPreview(const QString& content)
{
    const auto text=answer(content);QStringList rows;
    if(!text.startsWith('['))return rows;
    qsizetype i=1;
    while(i<text.size()){
        while(i<text.size()&&text[i].isSpace())++i;
        if(i==text.size()||text[i]!=QChar('"'))break;
        auto start=i++;QString value;bool closed=false;
        for(;i<text.size();++i){
            auto c=text[i];
            if(c=='"'){++i;closed=true;break;}
            if(c=='\\'){
                if(++i==text.size())break;c=text[i];
                if(c=='u'){
                    if(i+4>=text.size())break;bool ok=false;auto code=text.mid(i+1,4).toUShort(&ok,16);if(!ok)return rows;
                    value+=QChar(code);i+=4;
                }else if(c=='n')value+='\n';else if(c=='r')value+='\r';else if(c=='t')value+='\t';
                else if(c=='b')value+='\b';else if(c=='f')value+='\f';
                else if(c=='"'||c=='\\'||c=='/')value+=c;else return rows;
            }else if(c.unicode()<32)return rows;else value+=c;
        }
        if(!value.isEmpty()&&value.back().isHighSurrogate())value.chop(1);
        rows<<value;if(!closed)break;
        while(i<text.size()&&text[i].isSpace())++i;
        if(i==text.size()||text[i]!=QChar(','))break;++i;
    }
    return rows;
}
void AiStream::event()
{
    if(m_event.isEmpty())return;auto bytes=m_event.trimmed();m_event.clear();
    if(bytes=="[DONE]"){m_done=true;return;}
    if(m_done)return;
    auto doc=QJsonDocument::fromJson(bytes);if(!doc.isObject())invalid("AI 流式响应格式不正确");
    auto obj=doc.object();if(obj.contains("error"))invalid("AI 流式服务返回错误");
    const auto choices=obj.value("choices").toArray();if(choices.isEmpty())return;
    const auto choice=choices.first().toObject();
    if(choice.value("index").toInt()!=0)return;
    if(!choice.value("finish_reason").isNull()){
        m_reason=choice.value("finish_reason").toString();reason(m_reason);
    }
    const auto chunk=choice.value("delta").toObject().value("content").toString();
    if(!chunk.isEmpty()){
        m_content+=chunk;if(m_content.size()>1024*1024)invalid("AI 响应过大");
        if(m_preview)m_preview(aiArrayPreview(m_content));
    }
}
void AiStream::feed(const char* data,qint64 size)
{
    m_raw.append(data,size);m_pending.append(data,size);
    for(;;){
        auto end=m_pending.indexOf('\n');if(end<0)break;
        auto line=m_pending.left(end);m_pending.remove(0,end+1);if(line.endsWith('\r'))line.chop(1);
        if(line.startsWith("data:")){
            m_sse=true;line.remove(0,5);if(line.startsWith(' '))line.remove(0,1);
            if(!m_event.isEmpty())m_event+='\n';m_event+=line;
        }else if(line.isEmpty()&&m_sse)event();
    }
}
QStringList AiStream::finish(int expected)
{
    if(m_sse){
        if(!m_pending.trimmed().isEmpty())invalid("AI 流式连接中断，请重试");
        event();if(!m_done&&m_reason!="stop")invalid("AI 流式连接中断，请重试");
    }else{
        auto doc=QJsonDocument::fromJson(m_raw);auto root=doc.object();
        if(root.contains("error"))invalid("AI 服务返回错误");
        auto choices=root.value("choices").toArray();if(choices.isEmpty())invalid("AI 返回格式不正确");
        auto choice=choices.first().toObject();reason(choice.value("finish_reason").toString());
        m_content=choice.value("message").toObject().value("content").toString();
    }
    auto doc=QJsonDocument::fromJson(answer(m_content).toUtf8());
    if(!doc.isArray()||doc.array().size()!=expected)invalid("AI 返回的文本块不完整");
    QStringList rows;for(auto value:doc.array()){
        if(!value.isString()||value.toString().trimmed().isEmpty())invalid("AI 返回空文本或格式不正确");
        rows<<value.toString();
    }
    return rows;
}
}
