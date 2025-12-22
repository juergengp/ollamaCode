#ifndef OLEG_EMBEDDINGS_H
#define OLEG_EMBEDDINGS_H

#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace oleg {

// Embedding vector type
using Embedding = std::vector<float>;

// Embedding result
struct EmbeddingResult {
    bool success;
    std::string error;
    Embedding embedding;
    int dimensions;
};

// Batch embedding result
struct BatchEmbeddingResult {
    bool success;
    std::string error;
    std::vector<Embedding> embeddings;
    int dimensions;
};

// Embedding provider interface
class EmbeddingProvider {
public:
    virtual ~EmbeddingProvider() = default;

    // Generate embedding for single text
    virtual EmbeddingResult embed(const std::string& text) = 0;

    // Generate embeddings for multiple texts
    virtual BatchEmbeddingResult embedBatch(const std::vector<std::string>& texts) = 0;

    // Get provider name
    virtual std::string getName() const = 0;

    // Get model name
    virtual std::string getModel() const = 0;

    // Get embedding dimensions
    virtual int getDimensions() const = 0;
};

// Ollama embedding provider
class OllamaEmbeddingProvider : public EmbeddingProvider {
public:
    explicit OllamaEmbeddingProvider(const std::string& host = "http://localhost:11434",
                                      const std::string& model = "nomic-embed-text");
    ~OllamaEmbeddingProvider() override = default;

    EmbeddingResult embed(const std::string& text) override;
    BatchEmbeddingResult embedBatch(const std::vector<std::string>& texts) override;

    std::string getName() const override { return "ollama"; }
    std::string getModel() const override { return model_; }
    int getDimensions() const override { return dimensions_; }

    // Set Ollama host
    void setHost(const std::string& host);

    // Set embedding model
    void setModel(const std::string& model);

    // Test connection
    bool testConnection();

    // List available embedding models
    std::vector<std::string> listModels();

private:
    std::string host_;
    std::string model_;
    int dimensions_;

    // Detect dimensions from first embedding
    void detectDimensions(const Embedding& emb);
};

// Local embedding provider (using simple TF-IDF or word2vec-like approach)
// This is a fallback when Ollama is not available
class LocalEmbeddingProvider : public EmbeddingProvider {
public:
    LocalEmbeddingProvider();
    ~LocalEmbeddingProvider() override = default;

    EmbeddingResult embed(const std::string& text) override;
    BatchEmbeddingResult embedBatch(const std::vector<std::string>& texts) override;

    std::string getName() const override { return "local"; }
    std::string getModel() const override { return "tfidf-256"; }
    int getDimensions() const override { return dimensions_; }

private:
    int dimensions_;

    // Simple hash-based embedding (bag of words style)
    Embedding hashEmbed(const std::string& text);

    // Tokenize text
    std::vector<std::string> tokenize(const std::string& text);

    // Hash function for tokens
    uint32_t hash(const std::string& token);
};

// Main embedding client
class EmbeddingClient {
public:
    EmbeddingClient();
    ~EmbeddingClient() = default;

    // Configure
    void setProvider(const std::string& provider);  // "ollama" or "local"
    void setOllamaHost(const std::string& host);
    void setOllamaModel(const std::string& model);

    // Get current provider info
    std::string getProvider() const;
    std::string getModel() const;
    int getDimensions() const;

    // Generate embeddings
    EmbeddingResult embed(const std::string& text);
    BatchEmbeddingResult embedBatch(const std::vector<std::string>& texts);

    // Utility functions
    static float cosineSimilarity(const Embedding& a, const Embedding& b);
    static float dotProduct(const Embedding& a, const Embedding& b);
    static Embedding normalize(const Embedding& emb);

    // Test if embeddings are available
    bool isAvailable();

private:
    std::string current_provider_;
    std::unique_ptr<OllamaEmbeddingProvider> ollama_;
    std::unique_ptr<LocalEmbeddingProvider> local_;

    EmbeddingProvider* getActiveProvider();
};

} // namespace oleg

#endif // OLEG_EMBEDDINGS_H
