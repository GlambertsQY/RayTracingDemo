#include "app/Application.h"
#include "common/Log.h"
#include "platform/Platform.h"

#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv)
{
    Options options;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--scene" && i + 1 < argc)
        {
            const std::string value = argv[++i];
            if (value == "sponza")
            {
                options.scene = 1;
            }
            else if (value == "showcase")
            {
                options.scene = 2;
            }
            else if (value == "gallery")
            {
                options.scene = 3;
            }
            else
            {
                options.scene = 0;
            }
        }
        else if (arg == "--width" && i + 1 < argc)
        {
            options.width = std::atoi(argv[++i]);
        }
        else if (arg == "--height" && i + 1 < argc)
        {
            options.height = std::atoi(argv[++i]);
        }
        else if (arg == "--frames" && i + 1 < argc)
        {
            options.frames = std::atoi(argv[++i]);
        }
        else if (arg == "--screenshot" && i + 1 < argc)
        {
            options.screenshot = argv[++i];
        }
        else if (arg == "--renderdoc-capture")
        {
            options.renderDocCapture = true;
        }
    }

    std::unique_ptr<Platform> platform = createPlatform();
    if (!platform->init("Vulkan Hardware Ray Tracing Demo", options.width, options.height))
    {
        RT_LOG("Platform initialization failed");
        platform->shutdown();
        return 1;
    }

    Application app;
    if (!app.init(*platform, options))
    {
        RT_LOG("Application initialization failed");
        app.shutdown();
        platform->shutdown();
        return 1;
    }

    app.run(*platform);
    app.shutdown();
    platform->shutdown();
    return 0;
}
