#ifndef CASPER_RAG_ENGINE_H
#define CASPER_RAG_ENGINE_H

#include "vector_db.h"
#include "embeddings.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace casper {

// Document chunk for indexing
struct DocumentChunk {
    std::string content;
    std::string source;
    int chunk_index;
    int total_chunks;
    std::string metadata;
};

// RAG context result
struct RAGContext {
    std::vector<VectorSearchResult> results;
    std::string formatted_context;
    int total_tokens_estimate;
};

// Learning result
struct LearnResult {
    bool success;
    std::string error;
    int documents_added;
    int chunks_created;
    std::string source;
};

// RAG Engine configuration
struct RAGConfig {
    bool enabled = true;
    bool auto_context = true;
    double similarity_threshold = 0.7;
    int max_chunks = 5;
    int chunk_size = 500;       // Characters per chunk
    int chunk_overlap = 50;     // Overlap between chunks
    int max_context_tokens = 2000;
};

// RAG Engine - orchestrates learning and retrieval
class RAGEngine {
public:
    RAGEngine();
    ~RAGEngine();

    // Initialize with vector DB and embeddings
    bool initialize(const std::string& vector_backend, const std::string& vector_path,
                   const std::string& embedding_provider, const std::string& ollama_host,
                   const std::string& embedding_model);

    // Configuration
    void setConfig(const RAGConfig& config);
    RAGConfig getConfig() const;

    // Learning operations
    LearnResult learnFile(const std::string& file_path);
    LearnResult learnDirectory(const std::string& dir_path, const std::string& pattern = "*");
    LearnResult learnText(const std::string& text, const std::string& source);
    LearnResult learnUrl(const std::string& url);

    // Forgetting operations
    bool forget(const std::string& source);
    bool forgetAll();

    // Retrieval operations
    RAGContext retrieve(const std::string& query, int max_results = -1);

    // Context injection for prompts
    std::string injectContext(const std::string& user_message);

    // Get learned sources
    std::vector<std::string> getSources();

    // Statistics
    VectorDBStats getStats();

    // Status
    bool isInitialized() const;
    bool isEnabled() const;

    // Progress callback for long operations
    void setProgressCallback(std::function<void(const std::string&, int, int)> callback);

private:
    std::unique_ptr<VectorDB> vector_db_;
    std::unique_ptr<EmbeddingClient> embedder_;
    RAGConfig config_;
    bool initialized_;
    std::function<void(const std::string&, int, int)> progress_callback_;

    // Helper methods
    std::vector<DocumentChunk> chunkText(const std::string& text, const std::string& source);
    std::string readFile(const std::string& path);
    std::vector<std::string> listFiles(const std::string& dir_path, const std::string& pattern);
    std::string formatContext(const std::vector<VectorSearchResult>& results);
    int estimateTokens(const std::string& text);
};

} // namespace casper

#endif // CASPER_RAG_ENGINE_H
