# Pointra 词典来源与备选渠道评估

核对日期：2026-11-07。Pointra 公开源码提交：`09aab5c6f30175f6603310857c37a4ccd35eac9a`。

## 查到的事实

| 项目 | 公开证据 | 能确认到什么程度 |
|---|---|---|
| 词典结果 | README 将 SearchContent 标为“搜狗查词结果展示”；公开结果结构包括 `wordCard`、`usualDict`、词形和音标 | 词典来源线索指向搜狗；实际服务模块没有公开，无法从仓库确认当前全部请求地址、签名和备用顺序 |
| 发音 | `src/main/utils/tool.ts` 优先使用返回的音频地址，无音频地址时使用 `dict.youdao.com/dictvoice` | 有道是发音备用来源，不足以说明释义也来自有道 |
| 本地词典 | `commands/translate.rs` 先查缓存，未命中时联网；成功且可缓存时写回 SQLite/LRU 缓存 | 保存查过的结果，不是内置可直接复制的完整离线词库 |
| 可复用接口代码 | README 明确说明非官方翻译 API 代码不公开；`.gitignore` 排除了 `src-tauri/src/utils/trans_server.rs` | 不能直接移植其服务实现，也不能把 Pointra 当作已有公开 API 渠道 |

## 对截图翻译插件的建议

可以增加词典备选功能，但词典释义和整句翻译应分别处理：

1. **英文单词：可选 ECDICT 离线词库。** 项目提供英中释义、英文释义、音标和词形，仓库声明 MIT。适合无网络时查询单词或已收录短语；不承担任意整句翻译。接入时按需导入词库并保留来源声明，避免默认安装包膨胀。是否收录某个词条必须查库，未命中继续使用现有渠道。
2. **整句翻译：可增加有道智云官方文本翻译渠道。** 官方接口需要应用 ID 和应用密钥，并按服务账户计费，可接入现有失败记忆和恢复机制。当前公开文档保证翻译结果及词典跳转链接；不能据此承诺返回 Pointra 那样的完整词典卡片。
3. **搜狗网页接口：暂不直接列为已可用渠道。** Pointra 未公开实际实现，网页服务也不能等同于稳定开发者 API。若后续接入，需要单独验证单词、整句、语言方向、错误与限流，不能用模拟测试声称真实服务可用。

本次只完成来源核对，没有新增或默认启用未经验证的服务，也没有调用使用者的密钥。

## 一手资料

- [Pointra README 与 API 说明](https://github.com/SYJun404/Pointra-Pub/blob/09aab5c6f30175f6603310857c37a4ccd35eac9a/README.md)
- [未公开模块清单](https://github.com/SYJun404/Pointra-Pub/blob/09aab5c6f30175f6603310857c37a4ccd35eac9a/.gitignore)
- [发音地址处理](https://github.com/SYJun404/Pointra-Pub/blob/09aab5c6f30175f6603310857c37a4ccd35eac9a/src/main/utils/tool.ts)
- [缓存与联网查询流程](https://github.com/SYJun404/Pointra-Pub/blob/09aab5c6f30175f6603310857c37a4ccd35eac9a/src-tauri/src/commands/translate.rs)
- [ECDICT 数据与字段说明](https://github.com/skywind3000/ECDICT)
- [有道智云官方文本翻译文档](https://ai.youdao.com/DOCSIRMA/html/trans/api/wbfy/index.html)
