#ifndef ICON_H
#define ICON_H

#include <napi.h>

namespace MacService {

// 获取应用程序或文件的图标
Napi::Value GetIcon(const Napi::CallbackInfo& info);

} // namespace MacService

#endif // ICON_H

