#ifndef CASPER_CONFIG_H
#define CASPER_CONFIG_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <sqlite3.h>

namespace casper {

// MCP Server configuration structure
struct MCPServerConfig {
    std::string name;
    std::string command;
    std::vector<std::string> args;
    std::map<std::string, std::string> env;
    bool enabled;
    std::string transport;  // "stdio" or "http"
    std::string url;        // For HTTP transport
};

class Config {
public:
    Config();
    ~Config();

    // Initialize/load configuration
    bool initialize(const std::string& config_path = "");

    // Getters
    std::string getModel() const { return model_; }
    std::string getOllamaHost() const { return ollama_host_; }
    double getTemperature() const { return temperature_; }
    int getMaxTokens() const { return max_tokens_; }
    bool getSafeMode() const { return safe_mode_; }
    bool getAutoApprove() const { return auto_approve_; }
    bool getMCPEnabled() const { return mcp_enabled_; }

    // Search settings
    std::string getSearchProvider() const { return search_provider_; }
    std::string getSearchApiKey() const { return search_api_key_; }

    // Database settings
    std::string getDBType() const { return db_type_; }
    std::string getDBConnection() const { return db_connection_; }
    bool getDBAllowWrite() const { return db_allow_write_; }

    // Vector database settings
    std::string getVectorBackend() const { return vector_backend_; }
    std::string getVectorPath() const { return vector_path_; }
    std::string getVectorUrl() const { return vector_url_; }

    // Embedding settings
    std::string getEmbeddingProvider() const { return embedding_provider_; }
    std::string getEmbeddingModel() const { return embedding_model_; }

    // RAG settings
    bool getRAGEnabled() const { return rag_enabled_; }
    bool getRAGAutoContext() const { return rag_auto_context_; }
    double getRAGSimilarityThreshold() const { return rag_similarity_threshold_; }
    int getRAGMaxChunks() const { return rag_max_chunks_; }

    // Setters
    void setModel(const std::string& model);
    void setOllamaHost(const std::string& host);
    void setTemperature(double temp);
    void setMaxTokens(int tokens);
    void setSafeMode(bool enabled);
    void setAutoApprove(bool enabled);
    void setMCPEnabled(bool enabled);

    // Search setters
    void setSearchProvider(const std::string& provider);
    void setSearchApiKey(const std::string& key);

    // Database setters
    void setDBType(const std::string& type);
    void setDBConnection(const std::string& conn);
    void setDBAllowWrite(bool allow);

    // Vector database setters
    void setVectorBackend(const std::string& backend);
    void setVectorPath(const std::string& path);
    void setVectorUrl(const std::string& url);

    // Embedding setters
    void setEmbeddingProvider(const std::string& provider);
    void setEmbeddingModel(const std::string& model);

    // RAG setters
    void setRAGEnabled(bool enabled);
    void setRAGAutoContext(bool enabled);
    void setRAGSimilarityThreshold(double threshold);
    void setRAGMaxChunks(int chunks);

    // Persistence
    bool save();
    bool load();

    // Allowed commands for safe mode
    bool isCommandAllowed(const std::string& command) const;
    void addAllowedCommand(const std::string& command);

    // MCP Server management
    std::vector<MCPServerConfig> getMCPServers() const;
    bool addMCPServer(const MCPServerConfig& server);
    bool removeMCPServer(const std::string& name);
    bool enableMCPServer(const std::string& name, bool enabled);
    MCPServerConfig* getMCPServer(const std::string& name);

    // Get config directory path
    static std::string getConfigDir();
    static std::string getConfigPath();
    static std::string getHistoryPath();
    static std::string getMCPConfigPath();
    static std::string getDefaultVectorPath();

private:
    void createDefaultConfig();
    void initializeDatabase();
    void loadAllowedCommands();
    void loadMCPServers();
    void saveMCPServers();

    sqlite3* db_;
    std::string config_path_;

    // Configuration values
    std::string model_;
    std::string ollama_host_;
    double temperature_;
    int max_tokens_;
    bool safe_mode_;
    bool auto_approve_;
    bool mcp_enabled_;

    // Search settings
    std::string search_provider_;
    std::string search_api_key_;

    // Database settings
    std::string db_type_;
    std::string db_connection_;
    bool db_allow_write_;

    // Vector database settings
    std::string vector_backend_;
    std::string vector_path_;
    std::string vector_url_;

    // Embedding settings
    std::string embedding_provider_;
    std::string embedding_model_;

    // RAG settings
    bool rag_enabled_;
    bool rag_auto_context_;
    double rag_similarity_threshold_;
    int rag_max_chunks_;

    // Allowed commands for safe mode
    std::vector<std::string> allowed_commands_;

    // MCP Servers
    std::vector<MCPServerConfig> mcp_servers_;
};

} // namespace casper

#endif // CASPER_CONFIG_H
