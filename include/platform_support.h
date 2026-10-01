#pragma once

#include <string>

namespace PlatformSupport
{
    std::string XmlEscape(const std::string& value);

    std::string BuildLaunchAgentPlist(const std::string& executablePath,
                                      const std::string& stdoutPath,
                                      const std::string& stderrPath);
}
