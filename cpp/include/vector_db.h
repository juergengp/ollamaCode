#ifndef OLLAMACODE_VECTOR_DB_H
#define OLLAMACODE_VECTOR_DB_H

#include "embeddings.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace ollamacode {

// Document stored in vector database
struct VectorDocument {
    std::string id;
    std::string content;
    std::string source;       // File path, URL, or custom identifier
    std::string metadata;     // JSON metadata
    Embedding embedding;
    int64_t timestamp;
};

// Search result
struct VectorSearchResult {
    VectorDocument document;
    float score;              // Similarity score (0-1)
    float distance;           // Raw distance
};

// Vector database statistics
struct VectorDBStats {
    int64_t document_count;
    int dimensions;
    std::string backend;
    std::string path;
    int64_t size_bytes;
};

// Vector database backend interface
class VectorDBBackend {
public:
    virtual ~VectorDBBackend() = default;

    // Lifecycle
    virtual bool open(const std::string& path) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // CRUD operations
    virtual bool insert(const VectorDocument& doc) = 0;
    virtual bool insertBatch(const std::vector<VectorDocument>& docs) = 0;
    virtual bool update(const VectorDocument& doc) = 0;
    virtual bool remove(const std::string& id) = 0;
    virtual bool removeBySource(const std::string& source) = 0;

    // Search
    virtual std::vector<VectorSearchResult> search(const Embedding& query, int top_k = 10, float threshold = 0.0f) = 0;

    // Retrieval
    virtual VectorDocument get(const std::string& id) = 0;
    virtual std::vector<VectorDocument> getBySource(const std::string& source) = 0;
    virtual std::vector<VectorDocument> getAll(int limit = 1000, int offset = 0) = 0;

    // Metadata
    virtual VectorDBStats getStats() = 0;
    virtual std::string getName() const = 0;

    // Maintenance
    virtual bool optimize() = 0;
    virtual bool clear() = 0;
};

// SQLite-based vector database (using manual similarity calculation)
class SQLiteVectorDB : public VectorDBBackend {
public:
    SQLiteVectorDB();
    ~SQLiteVectorDB() override;

    bool open(const std::string& path) override;
    void close() override;
    bool isOpen() const override;

    bool insert(const VectorDocument& doc) override;
    bool insertBatch(const std::vector<VectorDocument>& docs) override;
    bool update(const VectorDocument& doc) override;
    bool remove(const std::string& id) override;
    bool removeBySource(const std::string& source) override;

    std::vector<VectorSearchResult> search(const Embedding& query, int top_k = 10, float threshold = 0.0f) override;

    VectorDocument get(const std::string& id) override;
    std::vector<VectorDocument> getBySource(const std::string& source) override;
    std::vector<VectorDocument> getAll(int limit = 1000, int offset = 0) override;

    VectorDBStats getStats() override;
    std::string getName() const override { return "sqlite"; }

    bool optimize() override;
    bool clear() override;

private:
    void* db_;  // sqlite3*
    std::string db_path_;
    int dimensions_;

    void initializeTables();
    std::string serializeEmbedding(const Embedding& emb);
    Embedding deserializeEmbedding(const std::string& data);
    std::string generateId();
};

// ChromaDB backend (HTTP-based)
class ChromaDBBackend : public VectorDBBackend {
public:
    ChromaDBBackend();
    ~ChromaDBBackend() override;

    bool open(const std::string& url) override;  // URL format: http://host:port/collection_name
    void close() override;
    bool isOpen() const override;

    bool insert(const VectorDocument& doc) override;
    bool insertBatch(const std::vector<VectorDocument>& docs) override;
    bool update(const VectorDocument& doc) override;
    bool remove(const std::string& id) override;
    bool removeBySource(const std::string& source) override;

    std::vector<VectorSearchResult> search(const Embedding& query, int top_k = 10, float threshold = 0.0f) override;

    VectorDocument get(const std::string& id) override;
    std::vector<VectorDocument> getBySource(const std::string& source) override;
    std::vector<VectorDocument> getAll(int limit = 1000, int offset = 0) override;

    VectorDBStats getStats() override;
    std::string getName() const override { return "chroma"; }

    bool optimize() override;
    bool clear() override;

private:
    std::string base_url_;
    std::string collection_name_;
    bool connected_;

    std::string httpRequest(const std::string& method, const std::string& endpoint, const std::string& body = "");
};

#ifdef HAVE_FAISS
// FAISS backend
class FAISSBackend : public VectorDBBackend {
public:
    FAISSBackend();
    ~FAISSBackend() override;

    bool open(const std::string& path) override;
    void close() override;
    bool isOpen() const override;

    bool insert(const VectorDocument& doc) override;
    bool insertBatch(const std::vector<VectorDocument>& docs) override;
    bool update(const VectorDocument& doc) override;
    bool remove(const std::string& id) override;
    bool removeBySource(const std::string& source) override;

    std::vector<VectorSearchResult> search(const Embedding& query, int top_k = 10, float threshold = 0.0f) override;

    VectorDocument get(const std::string& id) override;
    std::vector<VectorDocument> getBySource(const std::string& source) override;
    std::vector<VectorDocument> getAll(int limit = 1000, int offset = 0) override;

    VectorDBStats getStats() override;
    std::string getName() const override { return "faiss"; }

    bool optimize() override;
    bool clear() override;

private:
    void* index_;  // faiss::Index*
    std::string index_path_;
    std::vector<VectorDocument> documents_;  // Metadata storage
    int dimensions_;
};
#endif

// Main vector database client
class VectorDB {
public:
    VectorDB();
    ~VectorDB();

    // Configuration
    bool open(const std::string& backend, const std::string& path);
    void close();
    bool isOpen() const;

    // Get/set backend
    std::string getBackend() const;
    std::string getPath() const;

    // Document operations
    bool add(const std::string& content, const std::string& source, const Embedding& embedding, const std::string& metadata = "");
    bool addBatch(const std::vector<std::string>& contents, const std::vector<std::string>& sources, const std::vector<Embedding>& embeddings);
    bool remove(const std::string& id);
    bool removeBySource(const std::string& source);

    // Search
    std::vector<VectorSearchResult> search(const Embedding& query, int top_k = 10, float threshold = 0.0f);
    std::vector<VectorSearchResult> searchByText(const std::string& query, EmbeddingClient& embedder, int top_k = 10, float threshold = 0.0f);

    // Retrieval
    VectorDocument get(const std::string& id);
    std::vector<VectorDocument> getBySource(const std::string& source);

    // Statistics
    VectorDBStats getStats();

    // Maintenance
    bool optimize();
    bool clear();

    // Export/Import
    bool exportTo(const std::string& path);
    bool importFrom(const std::string& path);

    // Available backends
    static std::vector<std::string> getAvailableBackends();

private:
    std::unique_ptr<VectorDBBackend> backend_;
    std::string backend_name_;
    std::string path_;
};

} // namespace ollamacode

#endif // OLLAMACODE_VECTOR_DB_H
