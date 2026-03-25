#ifndef MEDIA_H
#define MEDIA_H

#include <napi.h>

namespace MacService {

// 获取媒体状态
Napi::Value GetMediaState(const Napi::CallbackInfo& info);

// 设置媒体状态
Napi::Value SetMediaState(const Napi::CallbackInfo& info);

} // namespace MacService

#endif // MEDIA_H

