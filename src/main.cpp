#include "application.h"
#include <iostream>

int main(int argc, char* argv[]) {

    HairViewer app;
    try
    {
        Systems::RendererSettings settings{};
        settings.samplesMSAA = MSAASamples::x8;
        settings.clearColor  = Vec4(0.02, 0.02, 0.02, 1.0);
        settings.enableUI    = true;
        // settings.renderingType = RendererType::TFORWARD;
        // settings.shadowResolution = ShadowResolution::MEDIUM;

        int         maxFrames{0};
        LogLevel    logLevel{LogLevel::Warn};
        std::string logFile{};

        if (argc == 1)
            std::cout << "No arguments submitted, initializing with default parameters..." << std::endl;

        for (int i = 0; i < argc; ++i)
        {
            std::string token(argv[i]);
            if (token == "-type")
            {
                if (i + 1 >= argc)
                {
                    std::cerr << "\"-type\" argument expects a rendering type keyword:" << std::endl;
                    std::cerr << "[forward]" << std::endl;
                    std::cerr << "[deferred]" << std::endl;
                    return EXIT_FAILURE;
                }
                std::string type(argv[i + 1]);

                if (type == "forward")
                {
                    // settings.renderingType = RendererType::TFORWARD;
                    i++;
                    continue;
                }
                if (type == "deferred")
                {
                    // settings.renderingType = RendererType::TDEFERRED;
                    i++;
                    continue;
                }

                std::cerr << "\"--type\" invalid argument:" << std::endl;
                std::cerr << "[forward]" << std::endl;
                std::cerr << "[deferred]" << std::endl;
                return EXIT_FAILURE;
            } else if (token == "-aa")
            {
                if (i + 1 >= argc)
                {
                    std::cerr << "\"-aa\" argument expects an antialiasing type keyword:" << std::endl;
                    std::cerr << "[none]" << std::endl;
                    std::cerr << "[msaa4]" << std::endl;
                    std::cerr << "[msaa8]" << std::endl;
                    std::cerr << "[fxaa]" << std::endl;
                    return EXIT_FAILURE;
                }
                std::string aaType(argv[i + 1]);
                if (aaType == "none")
                    settings.samplesMSAA = MSAASamples::x1;
                if (aaType == "msaa4")
                    settings.samplesMSAA = MSAASamples::x4;
                if (aaType == "msaa8")
                    settings.samplesMSAA = MSAASamples::x8;
                // if (aaType == "fxaa"){}
                //     settings.AAtype = AntialiasingType::FXAA;

                i++;
                continue;
            } else if (token == "-gui")
            {
                if (i + 1 >= argc)
                {
                    std::cerr << "\"-gui\" argument expects an enabling gui type keyword:" << std::endl;
                    std::cerr << "[false]" << std::endl;
                    std::cerr << "[true]" << std::endl;
                    return EXIT_FAILURE;
                }
                std::string enableGui(argv[i + 1]);
                if (enableGui == "true")
                    settings.enableUI = true;
                if (enableGui == "false")
                    settings.enableUI = false;
                i++;
                continue;
            } else if (token == "--frames")
            {
                if (i + 1 >= argc)
                {
                    std::cerr << "\"--frames\" expects a positive integer" << std::endl;
                    return EXIT_FAILURE;
                }
                maxFrames = std::stoi(argv[++i]);
                continue;
            } else if (token == "--log-level")
            {
                if (i + 1 >= argc)
                {
                    std::cerr << "\"--log-level\" expects: error | warn | verbose" << std::endl;
                    return EXIT_FAILURE;
                }
                std::string level(argv[++i]);
                if (level == "error")
                    logLevel = LogLevel::Error;
                else if (level == "warn")
                    logLevel = LogLevel::Warn;
                else if (level == "verbose")
                    logLevel = LogLevel::Info;
                else
                {
                    std::cerr << "\"--log-level\" invalid value: " << level << " (expected: error | warn | verbose)" << std::endl;
                    return EXIT_FAILURE;
                }
                continue;
            } else if (token == "--log-file")
            {
                if (i + 1 >= argc)
                {
                    std::cerr << "\"--log-file\" expects a file path" << std::endl;
                    return EXIT_FAILURE;
                }
                logFile = argv[++i];
                continue;
            }
            continue;
        }

        // In frame-limited (test) mode, always write to a log file
        if (maxFrames > 0 && logFile.empty())
            logFile = "debug_trace.log";

        Logger::init(logLevel, logFile);

        app.set_max_frames(maxFrames);
        app.run(settings);
    } catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
        Logger::shutdown();
        return EXIT_FAILURE;
    }

    Logger::shutdown();
    return EXIT_SUCCESS;
}