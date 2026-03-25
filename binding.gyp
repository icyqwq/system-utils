{
  "targets": [
    {
      "target_name": "system_utils_native",
      "cflags!": [ "-fno-exceptions" ],
      "cflags_cc!": [ "-fno-exceptions" ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")"
      ],
      "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ],
      "conditions": [
        ["OS=='win'", {
          "sources": [
            "native/win/node_system_module.cpp",
            "native/win/SystemModuleManager.cpp"
          ],
          "win_delay_load_hook": "true",
          "msvs_settings": {
            "VCCLCompilerTool": {
              "ExceptionHandling": 1,
              "AdditionalOptions": [
                "/std:c++20",
                "/await"
              ]
            },
            "VCLinkerTool": {
              "DelayLoadDLLs": ["node.exe"]
            }
          },
          "defines": [
            "WINRT_LEAN_AND_MEAN",
            "_WIN32_WINNT=0x0A00",
            "NAPI_DISABLE_CPP_EXCEPTIONS"
          ],
          "libraries": [
            "windowsapp.lib",
            "delayimp.lib"
          ]
        }],
        ["OS=='mac'", {
          "sources": [
            "native/mac/main.cpp",
            "native/mac/keyboard.cpp",
            "native/mac/media.cpp",
            "native/mac/icon.mm",
            "native/mac/macro.mm"
          ],
          "xcode_settings": {
            "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
            "CLANG_CXX_LIBRARY": "libc++",
            "MACOSX_DEPLOYMENT_TARGET": "10.13",
            "ARCHS": ["x86_64", "arm64"],
            "CLANG_ENABLE_OBJC_ARC": "YES",
            "OTHER_LDFLAGS": [
              "-arch x86_64",
              "-arch arm64",
              "-framework CoreGraphics",
              "-framework Carbon",
              "-framework ApplicationServices",
              "-framework CoreAudio",
              "-framework AudioToolbox",
              "-framework Cocoa",
              "-framework AppKit",
              "-framework Foundation"
            ]
          },
          "cflags": [
            "-mmacosx-version-min=10.13"
          ],
          "cflags_cc": [
            "-std=c++17",
            "-mmacosx-version-min=10.13"
          ],
          "ldflags": [
            "-mmacosx-version-min=10.13"
          ],
          "libraries": [
            "-framework CoreGraphics",
            "-framework Carbon",
            "-framework ApplicationServices",
            "-framework CoreAudio",
            "-framework AudioToolbox",
            "-framework Cocoa",
            "-framework AppKit",
            "-framework Foundation"
          ]
        }]
      ]
    }
  ]
}
