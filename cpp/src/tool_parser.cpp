#include "tool_parser.h"
#include "utils.h"
#include <regex>
#include <iostream>
#include <sstream>

namespace oleg {

std::string ToolParser::extractTag(const std::string& text, const std::string& tag) {
    std::string openTag = "<" + tag + ">";
    std::string closeTag = "</" + tag + ">";

    size_t start = text.find(openTag);
    if (start == std::string::npos) return "";

    start += openTag.length();
    size_t end = text.find(closeTag, start);
    if (end == std::string::npos) return "";

    return text.substr(start, end - start);
}

std::vector<std::string> ToolParser::extractAllTags(const std::string& text, const std::string& tag) {
    std::vector<std::string> results;
    std::string openTag = "<" + tag + ">";
    std::string closeTag = "</" + tag + ">";

    size_t pos = 0;
    while (true) {
        size_t start = text.find(openTag, pos);
        if (start == std::string::npos) break;

        start += openTag.length();
        size_t end = text.find(closeTag, start);
        if (end == std::string::npos) break;

        results.push_back(text.substr(start, end - start));
        pos = end + closeTag.length();
    }

    return results;
}

std::string ToolParser::extractParameter(const std::string& text, const std::string& param) {
    std::string openTag = "<" + param + ">";
    std::string closeTag = "</" + param + ">";

    size_t start = text.find(openTag);
    if (start == std::string::npos) return "";

    start += openTag.length();
    size_t end = text.find(closeTag, start);
    if (end == std::string::npos) return "";

    std::string value = text.substr(start, end - start);
    return utils::trim(value);
}

// Extract attribute value from tag like <invoke name="Bash">
std::string ToolParser::extractAttribute(const std::string& tag, const std::string& attr) {
    std::string pattern = attr + "=\"";
    size_t start = tag.find(pattern);
    if (start == std::string::npos) return "";

    start += pattern.length();
    size_t end = tag.find("\"", start);
    if (end == std::string::npos) return "";

    return tag.substr(start, end - start);
}

bool ToolParser::hasToolCalls(const std::string& response) {
    // Support both old format and Claude's antml format
    return response.find("<tool_calls>") != std::string::npos ||
           response.find("<function_calls>") != std::string::npos;
}

// Parse Claude's function_calls format
std::vector<ToolCall> ToolParser::parseAntmlFormat(const std::string& response) {
    std::vector<ToolCall> toolCalls;

    // Find all invoke blocks
    std::string invokeStart = "<invoke name=\"";
    std::string invokeEnd = "</invoke>";

    size_t pos = 0;
    while ((pos = response.find(invokeStart, pos)) != std::string::npos) {
        ToolCall toolCall;
        
        // Extract tool name from <invoke name="ToolName">
        size_t nameStart = pos + invokeStart.length();
        size_t nameEnd = response.find("\"", nameStart);
        if (nameEnd == std::string::npos) break;
        
        toolCall.name = response.substr(nameStart, nameEnd - nameStart);
        
        // Find the end of this invoke block
        size_t blockEnd = response.find(invokeEnd, pos);
        if (blockEnd == std::string::npos) break;
        
        std::string block = response.substr(pos, blockEnd - pos);
        
        // Extract parameters from <parameter name="...">value</parameter>
        std::string paramStart = "<parameter name=\"";
        std::string paramEnd = "</parameter>";
        
        size_t pPos = 0;
        while ((pPos = block.find(paramStart, pPos)) != std::string::npos) {
            // Get parameter name
            size_t pNameStart = pPos + paramStart.length();
            size_t pNameEnd = block.find("\"", pNameStart);
            if (pNameEnd == std::string::npos) break;
            
            std::string paramName = block.substr(pNameStart, pNameEnd - pNameStart);
            
            // Find the > after the parameter name
            size_t valueStart = block.find(">", pNameEnd);
            if (valueStart == std::string::npos) break;
            valueStart++;
            
            // Find the closing tag
            size_t valueEnd = block.find(paramEnd, valueStart);
            if (valueEnd == std::string::npos) break;
            
            std::string paramValue = block.substr(valueStart, valueEnd - valueStart);
            toolCall.parameters[paramName] = paramValue;
            
            pPos = valueEnd + paramEnd.length();
        }
        
        if (!toolCall.name.empty()) {
            toolCalls.push_back(toolCall);
        }
        
        pos = blockEnd + invokeEnd.length();
    }

    return toolCalls;
}

std::vector<ToolCall> ToolParser::parseToolCalls(const std::string& response) {
    std::vector<ToolCall> toolCalls;

    if (!hasToolCalls(response)) {
        return toolCalls;
    }

    // Check for Claude's antml format first
    if (response.find("<function_calls>") != std::string::npos) {
        return parseAntmlFormat(response);
    }

    // Fallback to old format
    // Extract the tool_calls block
    std::string toolCallsBlock = extractTag(response, "tool_calls");
    if (toolCallsBlock.empty()) {
        return toolCalls;
    }

    // Extract all individual tool_call blocks
    std::vector<std::string> toolCallBlocks = extractAllTags(toolCallsBlock, "tool_call");

    for (const auto& block : toolCallBlocks) {
        ToolCall toolCall;

        // Extract tool name
        toolCall.name = extractTag(block, "tool_name");
        if (toolCall.name.empty()) {
            continue; // Skip invalid tool calls
        }

        // Extract parameters block
        std::string parametersBlock = extractTag(block, "parameters");
        if (!parametersBlock.empty()) {
            // Common parameters to extract
            std::vector<std::string> paramNames = {
                "command", "description", "file_path", "content",
                "old_string", "new_string", "pattern", "path", "output_mode",
                // Aliases for common parameters
                "file", "filename", "text", "data", "body",
                "old", "new", "search", "replace", "find", "original", "replacement"
            };

            for (const auto& paramName : paramNames) {
                std::string value = extractParameter(parametersBlock, paramName);
                if (!value.empty()) {
                    toolCall.parameters[paramName] = value;
                }
            }
        }

        toolCalls.push_back(toolCall);
    }

    return toolCalls;
}

std::string ToolParser::extractResponseText(const std::string& response) {
    if (!hasToolCalls(response)) {
        return response;
    }

    std::string result = response;

    // Remove function_calls blocks
    size_t antmlStart = result.find("<function_calls>");
    size_t antmlEnd = result.find("</function_calls>");
    if (antmlStart != std::string::npos && antmlEnd != std::string::npos) {
        antmlEnd += std::string("</function_calls>").length();
        std::string before = result.substr(0, antmlStart);
        std::string after = result.substr(antmlEnd);
        result = before + after;
    }

    // Remove tool_calls blocks
    size_t toolCallsStart = result.find("<tool_calls>");
    size_t toolCallsEnd = result.find("</tool_calls>");
    if (toolCallsStart != std::string::npos && toolCallsEnd != std::string::npos) {
        toolCallsEnd += std::string("</tool_calls>").length();
        std::string before = result.substr(0, toolCallsStart);
        std::string after = result.substr(toolCallsEnd);
        result = before + after;
    }

    // Clean up and trim
    result = utils::trim(result);

    // Remove empty lines
    std::string cleaned;
    std::istringstream stream(result);
    std::string line;
    while (std::getline(stream, line)) {
        line = utils::trim(line);
        if (!line.empty()) {
            cleaned += line + "\n";
        }
    }

    return utils::trim(cleaned);
}

} // namespace oleg
