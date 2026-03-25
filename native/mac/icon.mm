#include "icon.h"
#import <Cocoa/Cocoa.h>
#import <AppKit/AppKit.h>
#include <vector>

namespace MacService {

// 辅助函数：将NSImage转换为PNG数据
std::vector<uint8_t> NSImageToPNG(NSImage* image, int size) {
    std::vector<uint8_t> result;
    
    @autoreleasepool {
        // 创建指定大小的图像
        NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:NULL
            pixelsWide:size
            pixelsHigh:size
            bitsPerSample:8
            samplesPerPixel:4
            hasAlpha:YES
            isPlanar:NO
            colorSpaceName:NSCalibratedRGBColorSpace
            bytesPerRow:0
            bitsPerPixel:0];
        
        if (bitmap) {
            [NSGraphicsContext saveGraphicsState];
            
            NSGraphicsContext* context = [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
            [NSGraphicsContext setCurrentContext:context];
            
            // 绘制图像
            [image drawInRect:NSMakeRect(0, 0, size, size)
                     fromRect:NSZeroRect
                    operation:NSCompositingOperationSourceOver
                     fraction:1.0];
            
            [NSGraphicsContext restoreGraphicsState];
            
            // 转换为PNG
            NSData* pngData = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
            
            if (pngData) {
                const uint8_t* bytes = (const uint8_t*)[pngData bytes];
                NSUInteger length = [pngData length];
                result.assign(bytes, bytes + length);
            }
            
            [bitmap release];
        }
    }
    
    return result;
}

// 辅助函数：获取应用程序图标
NSImage* GetApplicationIcon(const std::string& appPath) {
    @autoreleasepool {
        NSString* path = [NSString stringWithUTF8String:appPath.c_str()];
        
        // 如果是应用程序名称，尝试通过NSWorkspace查找
        if (![path hasPrefix:@"/"] && ![path hasSuffix:@".app"]) {
            NSWorkspace* workspace = [NSWorkspace sharedWorkspace];
            NSString* appPath = [workspace fullPathForApplication:path];
            if (appPath) {
                path = appPath;
            }
        }
        
        // 获取图标
        NSImage* icon = [[NSWorkspace sharedWorkspace] iconForFile:path];
        if (icon) {
            return [icon retain];
        }
    }
    
    return nil;
}

// 辅助函数：获取文件图标
NSImage* GetFileIcon(const std::string& filePath) {
    @autoreleasepool {
        NSString* path = [NSString stringWithUTF8String:filePath.c_str()];
        NSImage* icon = [[NSWorkspace sharedWorkspace] iconForFile:path];
        if (icon) {
            return [icon retain];
        }
    }
    
    return nil;
}

// GetIcon - 获取图标
Napi::Value GetIcon(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    // 检查参数
    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "参数必须是一个对象").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    Napi::Object options = info[0].As<Napi::Object>();
    
    // 获取路径列表
    std::vector<std::string> paths;
    if (options.Has("path")) {
        Napi::Value pathValue = options.Get("path");
        if (pathValue.IsString()) {
            paths.push_back(pathValue.As<Napi::String>().Utf8Value());
        } else if (pathValue.IsArray()) {
            Napi::Array pathArray = pathValue.As<Napi::Array>();
            for (uint32_t i = 0; i < pathArray.Length(); i++) {
                Napi::Value item = pathArray[i];
                if (item.IsString()) {
                    paths.push_back(item.As<Napi::String>().Utf8Value());
                }
            }
        }
    }
    
    if (paths.empty()) {
        Napi::TypeError::New(env, "必须提供path参数").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    // 获取图标大小（默认128）
    int size = 128;
    if (options.Has("size") && options.Get("size").IsNumber()) {
        size = options.Get("size").As<Napi::Number>().Int32Value();
        if (size < 16) size = 16;
        if (size > 1024) size = 1024;
    }
    
    // 获取输出格式（png或buffer）
    std::string format = "buffer";
    if (options.Has("format") && options.Get("format").IsString()) {
        format = options.Get("format").As<Napi::String>().Utf8Value();
    }
    
    // 创建结果对象
    Napi::Object result = Napi::Object::New(env);
    Napi::Array icons = Napi::Array::New(env);
    
    // 处理每个路径
    for (size_t i = 0; i < paths.size(); i++) {
        const std::string& path = paths[i];
        
        NSImage* icon = nil;
        
        @autoreleasepool {
            // 尝试作为应用程序获取图标
            icon = GetApplicationIcon(path);
            
            // 如果失败，尝试作为文件获取图标
            if (!icon) {
                icon = GetFileIcon(path);
            }
            
            if (icon) {
                // 转换为PNG数据
                std::vector<uint8_t> pngData = NSImageToPNG(icon, size);
                
                if (!pngData.empty()) {
                    if (format == "base64") {
                        // Base64编码
                        static const char base64_chars[] = 
                            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                            "abcdefghijklmnopqrstuvwxyz"
                            "0123456789+/";
                        
                        std::string base64;
                        int val = 0;
                        int valb = -6;
                        
                        for (uint8_t c : pngData) {
                            val = (val << 8) + c;
                            valb += 8;
                            while (valb >= 0) {
                                base64.push_back(base64_chars[(val >> valb) & 0x3F]);
                                valb -= 6;
                            }
                        }
                        
                        if (valb > -6) {
                            base64.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
                        }
                        
                        while (base64.size() % 4) {
                            base64.push_back('=');
                        }
                        
                        icons[i] = Napi::String::New(env, base64);
                    } else {
                        // 返回Buffer
                        Napi::Buffer<uint8_t> buffer = Napi::Buffer<uint8_t>::Copy(env, pngData.data(), pngData.size());
                        icons[i] = buffer;
                    }
                } else {
                    icons[i] = env.Null();
                }
                
                [icon release];
            } else {
                icons[i] = env.Null();
            }
        }
    }
    
    result.Set("icons", icons);
    
    return result;
}

} // namespace MacService

