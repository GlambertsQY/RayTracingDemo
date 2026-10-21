#include "app/Application.h"
#include "common/Log.h"

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
            options.scene = (value == "sponza") ? 1 : 0;
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

    Application app;
    if (!app.init(options))
    {
        RT_LOG("Application initialization failed");
        app.shutdown();
        return 1;
    }

    app.run();
    app.shutdown();
    return 0;
}
