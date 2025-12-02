#ifndef OLLAMACODE_TOOL_PARSER_H
#define OLLAMACODE_TOOL_PARSER_H

#include <string>
#include <vector>
#include <map>

namespace ollamacode {

struct ToolCall {
    std::string name;
    std::map<std::string, std::string> parameters;
};

class ToolParser {
public:
    ToolParser() = default;

    // Parse tool calls from AI response
    std::vector<ToolCall> parseToolCalls(const std::string& response);

    // Extract non-tool text from response
    std::string extractResponseText(const std::string& response);

    // Check if response contains tool calls
    bool hasToolCalls(const std::string& response);

private:
    // XML parsing helpers
    std::string extractTag(const std::string& text, const std::string& tag);
    std::vector<std::string> extractAllTags(const std::string& text, const std::string& tag);
    std::string extractParameter(const std::string& text, const std::string& param);
    std::string extractAttribute(const std::string& tag, const std::string& attr);

    // Parse Claude's antml format
    std::vector<ToolCall> parseAntmlFormat(const std::string& response);
};

} // namespace ollamacode

#endif // OLLAMACODE_TOOL_PARSER_H
