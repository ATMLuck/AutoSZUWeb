#include "platform_support.h"

namespace PlatformSupport
{
    std::string XmlEscape(const std::string& value)
    {
        std::string escaped;
        escaped.reserve(value.size());
        for (char c : value)
        {
            switch (c)
            {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '\"': escaped += "&quot;"; break;
            case '\'': escaped += "&apos;"; break;
            default: escaped += c; break;
            }
        }
        return escaped;
    }

    std::string BuildLaunchAgentPlist(const std::string& executablePath,
                                      const std::string& stdoutPath,
                                      const std::string& stderrPath)
    {
        return
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
            "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
            "<plist version=\"1.0\">\n"
            "<dict>\n"
            "  <key>Label</key>\n"
            "  <string>com.autoszuweb.AutoSZUWeb</string>\n"
            "  <key>ProgramArguments</key>\n"
            "  <array><string>" + XmlEscape(executablePath) + "</string></array>\n"
            "  <key>RunAtLoad</key><true/>\n"
            "  <key>KeepAlive</key><false/>\n"
            "  <key>LimitLoadToSessionType</key><string>Aqua</string>\n"
            "  <key>StandardOutPath</key><string>" + XmlEscape(stdoutPath) + "</string>\n"
            "  <key>StandardErrorPath</key><string>" + XmlEscape(stderrPath) + "</string>\n"
            "</dict>\n"
            "</plist>\n";
    }
}
