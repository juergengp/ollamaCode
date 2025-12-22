#include "vector_db.h"
#include "json.hpp"
#include <sqlite3.h>
#include <curl/curl.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <fstream>
#include <chrono>
#include <random>
#include <iostream>
#include <sys/stat.h>

using json = nlohmann::json;

namespace oleg {

// CURL write callback
static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}

// ============================================================================
// SQLiteVectorDB Implementation
// ============================================================================

SQLiteVectorDB::SQLiteVectorDB() : db_(nullptr), dimensions_(0) {
}

SQLiteVectorDB::~SQLiteVectorDB() {
    close();
}

bool SQLiteVectorDB::open(const std::string& path) {
    if (db_) close();

    db_path_ = path;

    int rc = sqlite3_open(path.c_str(), reinterpret_cast<sqlite3**>(&db_));
    if (rc != SQLITE_OK) {
        std::cerr << "SQLite vector DB error: " << sqlite3_errmsg(static_cast<sqlite3*>(db_)) << std::endl;
        db_ = nullptr;
        return false;
    }

    initializeTables();
    return true;
}

void SQLiteVectorDB::close() {
    if (db_) {
        sqlite3_close(static_cast<sqlite3*>(db_));
        db_ = nullptr;
    }
}

bool SQLiteVectorDB::isOpen() const {
    return db_ != nullptr;
}

void SQLiteVectorDB::initializeTables() {
    const char* create_sql = R"(
        CREATE TABLE IF NOT EXISTS vectors (
            id TEXT PRIMARY KEY,
            content TEXT NOT NULL,
            source TEXT,
            metadata TEXT,
            embedding BLOB NOT NULL,
            dimensions INTEGER,
            timestamp INTEGER
        );
        CREATE INDEX IF NOT EXISTS idx_source ON vectors(source);
        CREATE INDEX IF NOT EXISTS idx_timestamp ON vectors(timestamp);
    )";

    char* err_msg = nullptr;
    sqlite3_exec(static_cast<sqlite3*>(db_), create_sql, nullptr, nullptr, &err_msg);
    if (err_msg) {
        std::cerr << "SQLite init error: " << err_msg << std::endl;
        sqlite3_free(err_msg);
    }
}

std::string SQLiteVectorDB::generateId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);

    std::stringstream ss;
    for (int i = 0; i < 32; i++) {
        ss << std::hex << dis(gen);
    }
    return ss.str();
}

std::string SQLiteVectorDB::serializeEmbedding(const Embedding& emb) {
    return std::string(reinterpret_cast<const char*>(emb.data()), emb.size() * sizeof(float));
}

Embedding SQLiteVectorDB::deserializeEmbedding(const std::string& data) {
    size_t count = data.size() / sizeof(float);
    Embedding emb(count);
    std::memcpy(emb.data(), data.data(), data.size());
    return emb;
}

bool SQLiteVectorDB::insert(const VectorDocument& doc) {
    if (!db_) return false;

    sqlite3_stmt* stmt;
    const char* sql = "INSERT OR REPLACE INTO vectors (id, content, source, metadata, embedding, dimensions, timestamp) VALUES (?, ?, ?, ?, ?, ?, ?)";

    if (sqlite3_prepare_v2(static_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    std::string id = doc.id.empty() ? generateId() : doc.id;
    std::string emb_data = serializeEmbedding(doc.embedding);
    int dims = static_cast<int>(doc.embedding.size());
    int64_t ts = doc.timestamp > 0 ? doc.timestamp :
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();

    if (dimensions_ == 0) dimensions_ = dims;

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, doc.content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, doc.source.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, doc.metadata.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 5, emb_data.data(), static_cast<int>(emb_data.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, dims);
    sqlite3_bind_int64(stmt, 7, ts);

    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

bool SQLiteVectorDB::insertBatch(const std::vector<VectorDocument>& docs) {
    if (!db_) return false;

    sqlite3_exec(static_cast<sqlite3*>(db_), "BEGIN TRANSACTION", nullptr, nullptr, nullptr);

    for (const auto& doc : docs) {
        if (!insert(doc)) {
            sqlite3_exec(static_cast<sqlite3*>(db_), "ROLLBACK", nullptr, nullptr, nullptr);
            return false;
        }
    }

    sqlite3_exec(static_cast<sqlite3*>(db_), "COMMIT", nullptr, nullptr, nullptr);
    return true;
}

bool SQLiteVectorDB::update(const VectorDocument& doc) {
    return insert(doc);  // INSERT OR REPLACE handles updates
}

bool SQLiteVectorDB::remove(const std::string& id) {
    if (!db_) return false;

    sqlite3_stmt* stmt;
    const char* sql = "DELETE FROM vectors WHERE id = ?";

    if (sqlite3_prepare_v2(static_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

bool SQLiteVectorDB::removeBySource(const std::string& source) {
    if (!db_) return false;

    sqlite3_stmt* stmt;
    const char* sql = "DELETE FROM vectors WHERE source = ?";

    if (sqlite3_prepare_v2(static_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, source.c_str(), -1, SQLITE_TRANSIENT);
    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

std::vector<VectorSearchResult> SQLiteVectorDB::search(const Embedding& query, int top_k, float threshold) {
    std::vector<VectorSearchResult> results;
    if (!db_) return results;

    // Load all vectors and compute similarity (brute force for SQLite)
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, content, source, metadata, embedding, timestamp FROM vectors";

    if (sqlite3_prepare_v2(static_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }

    std::vector<VectorSearchResult> all_results;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        VectorSearchResult res;
        res.document.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        res.document.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));

        const char* source = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        res.document.source = source ? source : "";

        const char* meta = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        res.document.metadata = meta ? meta : "";

        const void* blob = sqlite3_column_blob(stmt, 4);
        int blob_size = sqlite3_column_bytes(stmt, 4);
        res.document.embedding = deserializeEmbedding(std::string(static_cast<const char*>(blob), blob_size));

        res.document.timestamp = sqlite3_column_int64(stmt, 5);

        // Compute cosine similarity
        res.score = EmbeddingClient::cosineSimilarity(query, res.document.embedding);
        res.distance = 1.0f - res.score;

        if (res.score >= threshold) {
            all_results.push_back(res);
        }
    }

    sqlite3_finalize(stmt);

    // Sort by score descending
    std::sort(all_results.begin(), all_results.end(),
        [](const VectorSearchResult& a, const VectorSearchResult& b) {
            return a.score > b.score;
        });

    // Return top_k
    if (static_cast<int>(all_results.size()) > top_k) {
        all_results.resize(top_k);
    }

    return all_results;
}

VectorDocument SQLiteVectorDB::get(const std::string& id) {
    VectorDocument doc;
    if (!db_) return doc;

    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, content, source, metadata, embedding, timestamp FROM vectors WHERE id = ?";

    if (sqlite3_prepare_v2(static_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return doc;
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        doc.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        doc.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));

        const char* source = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        doc.source = source ? source : "";

        const char* meta = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        doc.metadata = meta ? meta : "";

        const void* blob = sqlite3_column_blob(stmt, 4);
        int blob_size = sqlite3_column_bytes(stmt, 4);
        doc.embedding = deserializeEmbedding(std::string(static_cast<const char*>(blob), blob_size));

        doc.timestamp = sqlite3_column_int64(stmt, 5);
    }

    sqlite3_finalize(stmt);
    return doc;
}

std::vector<VectorDocument> SQLiteVectorDB::getBySource(const std::string& source) {
    std::vector<VectorDocument> docs;
    if (!db_) return docs;

    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, content, source, metadata, embedding, timestamp FROM vectors WHERE source = ?";

    if (sqlite3_prepare_v2(static_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return docs;
    }

    sqlite3_bind_text(stmt, 1, source.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        VectorDocument doc;
        doc.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        doc.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));

        const char* src = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        doc.source = src ? src : "";

        const char* meta = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        doc.metadata = meta ? meta : "";

        const void* blob = sqlite3_column_blob(stmt, 4);
        int blob_size = sqlite3_column_bytes(stmt, 4);
        doc.embedding = deserializeEmbedding(std::string(static_cast<const char*>(blob), blob_size));

        doc.timestamp = sqlite3_column_int64(stmt, 5);

        docs.push_back(doc);
    }

    sqlite3_finalize(stmt);
    return docs;
}

std::vector<VectorDocument> SQLiteVectorDB::getAll(int limit, int offset) {
    std::vector<VectorDocument> docs;
    if (!db_) return docs;

    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, content, source, metadata, embedding, timestamp FROM vectors ORDER BY timestamp DESC LIMIT ? OFFSET ?";

    if (sqlite3_prepare_v2(static_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return docs;
    }

    sqlite3_bind_int(stmt, 1, limit);
    sqlite3_bind_int(stmt, 2, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        VectorDocument doc;
        doc.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        doc.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));

        const char* source = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        doc.source = source ? source : "";

        const char* meta = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        doc.metadata = meta ? meta : "";

        const void* blob = sqlite3_column_blob(stmt, 4);
        int blob_size = sqlite3_column_bytes(stmt, 4);
        doc.embedding = deserializeEmbedding(std::string(static_cast<const char*>(blob), blob_size));

        doc.timestamp = sqlite3_column_int64(stmt, 5);

        docs.push_back(doc);
    }

    sqlite3_finalize(stmt);
    return docs;
}

VectorDBStats SQLiteVectorDB::getStats() {
    VectorDBStats stats;
    stats.backend = "sqlite";
    stats.path = db_path_;
    stats.document_count = 0;
    stats.dimensions = dimensions_;
    stats.size_bytes = 0;

    if (!db_) return stats;

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(static_cast<sqlite3*>(db_), "SELECT COUNT(*) FROM vectors", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            stats.document_count = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    // Get file size
    struct stat st;
    if (stat(db_path_.c_str(), &st) == 0) {
        stats.size_bytes = st.st_size;
    }

    return stats;
}

bool SQLiteVectorDB::optimize() {
    if (!db_) return false;
    char* err_msg = nullptr;
    sqlite3_exec(static_cast<sqlite3*>(db_), "VACUUM", nullptr, nullptr, &err_msg);
    if (err_msg) {
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

bool SQLiteVectorDB::clear() {
    if (!db_) return false;
    char* err_msg = nullptr;
    sqlite3_exec(static_cast<sqlite3*>(db_), "DELETE FROM vectors", nullptr, nullptr, &err_msg);
    if (err_msg) {
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

// ============================================================================
// ChromaDBBackend Implementation
// ============================================================================

ChromaDBBackend::ChromaDBBackend() : connected_(false) {
}

ChromaDBBackend::~ChromaDBBackend() {
    close();
}

bool ChromaDBBackend::open(const std::string& url) {
    // Parse URL: http://host:port/collection_name
    size_t last_slash = url.rfind('/');
    if (last_slash == std::string::npos || last_slash == url.length() - 1) {
        base_url_ = url;
        collection_name_ = "default";
    } else {
        base_url_ = url.substr(0, last_slash);
        collection_name_ = url.substr(last_slash + 1);
    }

    // Test connection
    std::string response = httpRequest("GET", "/api/v1/heartbeat");
    connected_ = !response.empty();
    return connected_;
}

void ChromaDBBackend::close() {
    connected_ = false;
}

bool ChromaDBBackend::isOpen() const {
    return connected_;
}

std::string ChromaDBBackend::httpRequest(const std::string& method, const std::string& endpoint, const std::string& body) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string url = base_url_ + endpoint;
    std::string response;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return "";
    }

    return response;
}

bool ChromaDBBackend::insert(const VectorDocument& doc) {
    json request;
    request["ids"] = {doc.id.empty() ? std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) : doc.id};
    request["documents"] = {doc.content};
    request["embeddings"] = {doc.embedding};

    json metadata;
    metadata["source"] = doc.source;
    if (!doc.metadata.empty()) {
        try {
            metadata["custom"] = json::parse(doc.metadata);
        } catch (...) {
            metadata["custom"] = doc.metadata;
        }
    }
    request["metadatas"] = {metadata};

    std::string response = httpRequest("POST", "/api/v1/collections/" + collection_name_ + "/add", request.dump());
    return !response.empty();
}

bool ChromaDBBackend::insertBatch(const std::vector<VectorDocument>& docs) {
    json request;
    std::vector<std::string> ids;
    std::vector<std::string> documents;
    std::vector<std::vector<float>> embeddings;
    std::vector<json> metadatas;

    for (const auto& doc : docs) {
        ids.push_back(doc.id.empty() ? std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) : doc.id);
        documents.push_back(doc.content);
        embeddings.push_back(doc.embedding);

        json metadata;
        metadata["source"] = doc.source;
        metadatas.push_back(metadata);
    }

    request["ids"] = ids;
    request["documents"] = documents;
    request["embeddings"] = embeddings;
    request["metadatas"] = metadatas;

    std::string response = httpRequest("POST", "/api/v1/collections/" + collection_name_ + "/add", request.dump());
    return !response.empty();
}

bool ChromaDBBackend::update(const VectorDocument& doc) {
    json request;
    request["ids"] = {doc.id};
    request["documents"] = {doc.content};
    request["embeddings"] = {doc.embedding};

    std::string response = httpRequest("POST", "/api/v1/collections/" + collection_name_ + "/update", request.dump());
    return !response.empty();
}

bool ChromaDBBackend::remove(const std::string& id) {
    json request;
    request["ids"] = {id};

    std::string response = httpRequest("POST", "/api/v1/collections/" + collection_name_ + "/delete", request.dump());
    return !response.empty();
}

bool ChromaDBBackend::removeBySource(const std::string& source) {
    json request;
    request["where"] = {{"source", source}};

    std::string response = httpRequest("POST", "/api/v1/collections/" + collection_name_ + "/delete", request.dump());
    return !response.empty();
}

std::vector<VectorSearchResult> ChromaDBBackend::search(const Embedding& query, int top_k, float /*threshold*/) {
    std::vector<VectorSearchResult> results;

    json request;
    request["query_embeddings"] = {query};
    request["n_results"] = top_k;
    request["include"] = {"documents", "metadatas", "distances"};

    std::string response = httpRequest("POST", "/api/v1/collections/" + collection_name_ + "/query", request.dump());
    if (response.empty()) return results;

    try {
        json data = json::parse(response);

        if (data.contains("ids") && !data["ids"].empty()) {
            auto& ids = data["ids"][0];
            auto& documents = data["documents"][0];
            auto& distances = data["distances"][0];

            for (size_t i = 0; i < ids.size(); i++) {
                VectorSearchResult res;
                res.document.id = ids[i].get<std::string>();
                res.document.content = documents[i].get<std::string>();
                res.distance = distances[i].get<float>();
                res.score = 1.0f / (1.0f + res.distance);  // Convert distance to similarity
                results.push_back(res);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "ChromaDB search parse error: " << e.what() << std::endl;
    }

    return results;
}

VectorDocument ChromaDBBackend::get(const std::string& id) {
    VectorDocument doc;

    json request;
    request["ids"] = {id};
    request["include"] = {"documents", "metadatas", "embeddings"};

    std::string response = httpRequest("POST", "/api/v1/collections/" + collection_name_ + "/get", request.dump());
    if (response.empty()) return doc;

    try {
        json data = json::parse(response);
        if (data.contains("ids") && !data["ids"].empty()) {
            doc.id = data["ids"][0].get<std::string>();
            doc.content = data["documents"][0].get<std::string>();
            if (data.contains("embeddings") && !data["embeddings"].empty()) {
                doc.embedding = data["embeddings"][0].get<std::vector<float>>();
            }
        }
    } catch (...) {
        // Ignore parse errors
    }

    return doc;
}

std::vector<VectorDocument> ChromaDBBackend::getBySource(const std::string& source) {
    std::vector<VectorDocument> docs;

    json request;
    request["where"] = {{"source", source}};
    request["include"] = {"documents", "metadatas", "embeddings"};

    std::string response = httpRequest("POST", "/api/v1/collections/" + collection_name_ + "/get", request.dump());
    if (response.empty()) return docs;

    try {
        json data = json::parse(response);
        if (data.contains("ids")) {
            for (size_t i = 0; i < data["ids"].size(); i++) {
                VectorDocument doc;
                doc.id = data["ids"][i].get<std::string>();
                doc.content = data["documents"][i].get<std::string>();
                doc.source = source;
                docs.push_back(doc);
            }
        }
    } catch (...) {
        // Ignore parse errors
    }

    return docs;
}

std::vector<VectorDocument> ChromaDBBackend::getAll(int limit, int /*offset*/) {
    std::vector<VectorDocument> docs;

    json request;
    request["include"] = {"documents", "metadatas"};
    request["limit"] = limit;

    std::string response = httpRequest("POST", "/api/v1/collections/" + collection_name_ + "/get", request.dump());
    if (response.empty()) return docs;

    try {
        json data = json::parse(response);
        if (data.contains("ids")) {
            for (size_t i = 0; i < data["ids"].size(); i++) {
                VectorDocument doc;
                doc.id = data["ids"][i].get<std::string>();
                doc.content = data["documents"][i].get<std::string>();
                docs.push_back(doc);
            }
        }
    } catch (...) {
        // Ignore parse errors
    }

    return docs;
}

VectorDBStats ChromaDBBackend::getStats() {
    VectorDBStats stats;
    stats.backend = "chroma";
    stats.path = base_url_ + "/" + collection_name_;
    stats.document_count = 0;
    stats.dimensions = 0;
    stats.size_bytes = 0;

    std::string response = httpRequest("GET", "/api/v1/collections/" + collection_name_ + "/count");
    if (!response.empty()) {
        try {
            stats.document_count = std::stoll(response);
        } catch (...) {
            // Ignore
        }
    }

    return stats;
}

bool ChromaDBBackend::optimize() {
    return true;  // ChromaDB handles this automatically
}

bool ChromaDBBackend::clear() {
    json request;
    std::string response = httpRequest("POST", "/api/v1/collections/" + collection_name_ + "/delete", request.dump());
    return !response.empty();
}

// ============================================================================
// VectorDB Implementation
// ============================================================================

VectorDB::VectorDB() {
}

VectorDB::~VectorDB() {
    close();
}

bool VectorDB::open(const std::string& backend, const std::string& path) {
    close();

    backend_name_ = backend;
    path_ = path;

    if (backend == "sqlite") {
        backend_ = std::make_unique<SQLiteVectorDB>();
    } else if (backend == "chroma") {
        backend_ = std::make_unique<ChromaDBBackend>();
    }
#ifdef HAVE_FAISS
    else if (backend == "faiss") {
        backend_ = std::make_unique<FAISSBackend>();
    }
#endif
    else {
        std::cerr << "Unknown vector database backend: " << backend << std::endl;
        return false;
    }

    return backend_->open(path);
}

void VectorDB::close() {
    if (backend_) {
        backend_->close();
        backend_.reset();
    }
}

bool VectorDB::isOpen() const {
    return backend_ && backend_->isOpen();
}

std::string VectorDB::getBackend() const {
    return backend_name_;
}

std::string VectorDB::getPath() const {
    return path_;
}

bool VectorDB::add(const std::string& content, const std::string& source, const Embedding& embedding, const std::string& metadata) {
    if (!backend_) return false;

    VectorDocument doc;
    doc.content = content;
    doc.source = source;
    doc.embedding = embedding;
    doc.metadata = metadata;
    doc.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    return backend_->insert(doc);
}

bool VectorDB::addBatch(const std::vector<std::string>& contents, const std::vector<std::string>& sources, const std::vector<Embedding>& embeddings) {
    if (!backend_) return false;

    std::vector<VectorDocument> docs;
    for (size_t i = 0; i < contents.size(); i++) {
        VectorDocument doc;
        doc.content = contents[i];
        doc.source = i < sources.size() ? sources[i] : "";
        doc.embedding = i < embeddings.size() ? embeddings[i] : Embedding{};
        doc.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        docs.push_back(doc);
    }

    return backend_->insertBatch(docs);
}

bool VectorDB::remove(const std::string& id) {
    if (!backend_) return false;
    return backend_->remove(id);
}

bool VectorDB::removeBySource(const std::string& source) {
    if (!backend_) return false;
    return backend_->removeBySource(source);
}

std::vector<VectorSearchResult> VectorDB::search(const Embedding& query, int top_k, float threshold) {
    if (!backend_) return {};
    return backend_->search(query, top_k, threshold);
}

std::vector<VectorSearchResult> VectorDB::searchByText(const std::string& query, EmbeddingClient& embedder, int top_k, float threshold) {
    auto result = embedder.embed(query);
    if (!result.success) return {};
    return search(result.embedding, top_k, threshold);
}

VectorDocument VectorDB::get(const std::string& id) {
    if (!backend_) return {};
    return backend_->get(id);
}

std::vector<VectorDocument> VectorDB::getBySource(const std::string& source) {
    if (!backend_) return {};
    return backend_->getBySource(source);
}

VectorDBStats VectorDB::getStats() {
    if (!backend_) return {};
    return backend_->getStats();
}

bool VectorDB::optimize() {
    if (!backend_) return false;
    return backend_->optimize();
}

bool VectorDB::clear() {
    if (!backend_) return false;
    return backend_->clear();
}

bool VectorDB::exportTo(const std::string& path) {
    if (!backend_) return false;

    auto docs = backend_->getAll(100000, 0);

    json output;
    output["backend"] = backend_name_;
    output["documents"] = json::array();

    for (const auto& doc : docs) {
        json j;
        j["id"] = doc.id;
        j["content"] = doc.content;
        j["source"] = doc.source;
        j["metadata"] = doc.metadata;
        j["embedding"] = doc.embedding;
        j["timestamp"] = doc.timestamp;
        output["documents"].push_back(j);
    }

    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << output.dump(2);
    return true;
}

bool VectorDB::importFrom(const std::string& path) {
    if (!backend_) return false;

    std::ifstream file(path);
    if (!file.is_open()) return false;

    try {
        json data = json::parse(file);

        if (data.contains("documents")) {
            for (const auto& j : data["documents"]) {
                VectorDocument doc;
                doc.id = j.value("id", "");
                doc.content = j.value("content", "");
                doc.source = j.value("source", "");
                doc.metadata = j.value("metadata", "");
                doc.embedding = j.value("embedding", Embedding{});
                doc.timestamp = j.value("timestamp", 0LL);
                backend_->insert(doc);
            }
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Import error: " << e.what() << std::endl;
        return false;
    }
}

std::vector<std::string> VectorDB::getAvailableBackends() {
    std::vector<std::string> backends = {"sqlite", "chroma"};

#ifdef HAVE_FAISS
    backends.push_back("faiss");
#endif

    return backends;
}

} // namespace oleg
