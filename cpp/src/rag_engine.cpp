#include "rag_engine.h"
#include "search_client.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <dirent.h>
#include <sys/stat.h>
#include <iostream>
#include <fnmatch.h>

namespace oleg {

RAGEngine::RAGEngine() : initialized_(false) {
}

RAGEngine::~RAGEngine() {
}

bool RAGEngine::initialize(const std::string& vector_backend, const std::string& vector_path,
                          const std::string& embedding_provider, const std::string& ollama_host,
                          const std::string& embedding_model) {
    // Initialize embedding client
    embedder_ = std::make_unique<EmbeddingClient>();
    embedder_->setProvider(embedding_provider);
    embedder_->setOllamaHost(ollama_host);
    embedder_->setOllamaModel(embedding_model);

    // Initialize vector database
    vector_db_ = std::make_unique<VectorDB>();
    if (!vector_db_->open(vector_backend, vector_path)) {
        std::cerr << "Failed to open vector database at: " << vector_path << std::endl;
        return false;
    }

    initialized_ = true;
    return true;
}

void RAGEngine::setConfig(const RAGConfig& config) {
    config_ = config;
}

RAGConfig RAGEngine::getConfig() const {
    return config_;
}

bool RAGEngine::isInitialized() const {
    return initialized_;
}

bool RAGEngine::isEnabled() const {
    return config_.enabled && initialized_;
}

void RAGEngine::setProgressCallback(std::function<void(const std::string&, int, int)> callback) {
    progress_callback_ = callback;
}

std::vector<DocumentChunk> RAGEngine::chunkText(const std::string& text, const std::string& source) {
    std::vector<DocumentChunk> chunks;

    if (text.empty()) return chunks;

    int chunk_size = config_.chunk_size;
    int overlap = config_.chunk_overlap;

    // Split into sentences first for better chunk boundaries
    std::vector<std::string> sentences;
    std::regex sentence_regex(R"([.!?]+\s+)");
    std::sregex_token_iterator iter(text.begin(), text.end(), sentence_regex, -1);
    std::sregex_token_iterator end;

    for (; iter != end; ++iter) {
        std::string s = iter->str();
        if (!s.empty()) {
            sentences.push_back(s);
        }
    }

    // If no sentences found, split by newlines
    if (sentences.empty()) {
        std::istringstream iss(text);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty()) {
                sentences.push_back(line);
            }
        }
    }

    // Build chunks from sentences
    std::string current_chunk;
    int chunk_index = 0;

    for (const auto& sentence : sentences) {
        if (current_chunk.length() + sentence.length() > static_cast<size_t>(chunk_size) && !current_chunk.empty()) {
            // Save current chunk
            DocumentChunk chunk;
            chunk.content = current_chunk;
            chunk.source = source;
            chunk.chunk_index = chunk_index++;
            chunks.push_back(chunk);

            // Start new chunk with overlap
            if (overlap > 0 && current_chunk.length() > static_cast<size_t>(overlap)) {
                current_chunk = current_chunk.substr(current_chunk.length() - overlap);
            } else {
                current_chunk.clear();
            }
        }

        if (!current_chunk.empty() && !sentence.empty()) {
            current_chunk += " ";
        }
        current_chunk += sentence;
    }

    // Don't forget the last chunk
    if (!current_chunk.empty()) {
        DocumentChunk chunk;
        chunk.content = current_chunk;
        chunk.source = source;
        chunk.chunk_index = chunk_index;
        chunks.push_back(chunk);
    }

    // Set total chunks count
    for (auto& chunk : chunks) {
        chunk.total_chunks = static_cast<int>(chunks.size());
    }

    return chunks;
}

std::string RAGEngine::readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::vector<std::string> RAGEngine::listFiles(const std::string& dir_path, const std::string& pattern) {
    std::vector<std::string> files;

    DIR* dir = opendir(dir_path.c_str());
    if (!dir) return files;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;

        // Skip hidden files and special directories
        if (name[0] == '.') continue;

        std::string full_path = dir_path + "/" + name;
        struct stat st;
        if (stat(full_path.c_str(), &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            // Recurse into directories
            auto sub_files = listFiles(full_path, pattern);
            files.insert(files.end(), sub_files.begin(), sub_files.end());
        } else if (S_ISREG(st.st_mode)) {
            // Check pattern match
            if (pattern == "*" || fnmatch(pattern.c_str(), name.c_str(), 0) == 0) {
                files.push_back(full_path);
            }
        }
    }

    closedir(dir);
    return files;
}

int RAGEngine::estimateTokens(const std::string& text) {
    // Rough estimate: ~4 characters per token
    return static_cast<int>(text.length() / 4);
}

std::string RAGEngine::formatContext(const std::vector<VectorSearchResult>& results) {
    if (results.empty()) return "";

    std::stringstream ss;
    ss << "=== Relevant Context ===\n\n";

    for (size_t i = 0; i < results.size(); i++) {
        const auto& res = results[i];
        ss << "[" << (i + 1) << "] Source: " << res.document.source;
        ss << " (relevance: " << static_cast<int>(res.score * 100) << "%)\n";
        ss << res.document.content << "\n\n";
    }

    ss << "========================\n\n";
    return ss.str();
}

LearnResult RAGEngine::learnFile(const std::string& file_path) {
    LearnResult result;
    result.success = false;
    result.documents_added = 0;
    result.chunks_created = 0;
    result.source = file_path;

    if (!initialized_) {
        result.error = "RAG engine not initialized";
        return result;
    }

    // Read file content
    std::string content = readFile(file_path);
    if (content.empty()) {
        result.error = "Could not read file: " + file_path;
        return result;
    }

    // Chunk the content
    auto chunks = chunkText(content, file_path);
    if (chunks.empty()) {
        result.error = "No chunks created from file";
        return result;
    }

    // Generate embeddings and store
    int added = 0;
    for (size_t i = 0; i < chunks.size(); i++) {
        const auto& chunk = chunks[i];

        if (progress_callback_) {
            progress_callback_(file_path, static_cast<int>(i + 1), static_cast<int>(chunks.size()));
        }

        auto emb_result = embedder_->embed(chunk.content);
        if (!emb_result.success) {
            std::cerr << "Embedding failed for chunk " << i << ": " << emb_result.error << std::endl;
            continue;
        }

        std::string metadata = "{\"chunk_index\":" + std::to_string(chunk.chunk_index) +
                              ",\"total_chunks\":" + std::to_string(chunk.total_chunks) + "}";

        if (vector_db_->add(chunk.content, chunk.source, emb_result.embedding, metadata)) {
            added++;
        }
    }

    result.success = added > 0;
    result.documents_added = 1;
    result.chunks_created = added;
    return result;
}

LearnResult RAGEngine::learnDirectory(const std::string& dir_path, const std::string& pattern) {
    LearnResult result;
    result.success = false;
    result.documents_added = 0;
    result.chunks_created = 0;
    result.source = dir_path;

    if (!initialized_) {
        result.error = "RAG engine not initialized";
        return result;
    }

    auto files = listFiles(dir_path, pattern);
    if (files.empty()) {
        result.error = "No matching files found in directory";
        return result;
    }

    for (size_t i = 0; i < files.size(); i++) {
        if (progress_callback_) {
            progress_callback_(files[i], static_cast<int>(i + 1), static_cast<int>(files.size()));
        }

        auto file_result = learnFile(files[i]);
        if (file_result.success) {
            result.documents_added++;
            result.chunks_created += file_result.chunks_created;
        }
    }

    result.success = result.documents_added > 0;
    return result;
}

LearnResult RAGEngine::learnText(const std::string& text, const std::string& source) {
    LearnResult result;
    result.success = false;
    result.documents_added = 0;
    result.chunks_created = 0;
    result.source = source;

    if (!initialized_) {
        result.error = "RAG engine not initialized";
        return result;
    }

    auto chunks = chunkText(text, source);
    if (chunks.empty()) {
        result.error = "No chunks created from text";
        return result;
    }

    int added = 0;
    for (const auto& chunk : chunks) {
        auto emb_result = embedder_->embed(chunk.content);
        if (!emb_result.success) continue;

        if (vector_db_->add(chunk.content, chunk.source, emb_result.embedding)) {
            added++;
        }
    }

    result.success = added > 0;
    result.documents_added = 1;
    result.chunks_created = added;
    return result;
}

LearnResult RAGEngine::learnUrl(const std::string& url) {
    LearnResult result;
    result.success = false;
    result.documents_added = 0;
    result.chunks_created = 0;
    result.source = url;

    if (!initialized_) {
        result.error = "RAG engine not initialized";
        return result;
    }

    // Fetch URL content using web spider
    WebSpider spider;
    auto page = spider.fetch(url);

    if (!page.success) {
        result.error = "Failed to fetch URL: " + page.error;
        return result;
    }

    // Learn the page content
    return learnText(page.content, url);
}

bool RAGEngine::forget(const std::string& source) {
    if (!initialized_) return false;
    return vector_db_->removeBySource(source);
}

bool RAGEngine::forgetAll() {
    if (!initialized_) return false;
    return vector_db_->clear();
}

RAGContext RAGEngine::retrieve(const std::string& query, int max_results) {
    RAGContext context;
    context.total_tokens_estimate = 0;

    if (!initialized_ || !config_.enabled) {
        return context;
    }

    int k = max_results > 0 ? max_results : config_.max_chunks;

    // Generate query embedding
    auto emb_result = embedder_->embed(query);
    if (!emb_result.success) {
        return context;
    }

    // Search vector database
    context.results = vector_db_->search(emb_result.embedding, k, static_cast<float>(config_.similarity_threshold));

    // Format context
    context.formatted_context = formatContext(context.results);
    context.total_tokens_estimate = estimateTokens(context.formatted_context);

    return context;
}

std::string RAGEngine::injectContext(const std::string& user_message) {
    if (!config_.auto_context || !initialized_ || !config_.enabled) {
        return user_message;
    }

    auto context = retrieve(user_message);

    if (context.results.empty() || context.formatted_context.empty()) {
        return user_message;
    }

    // Limit context size
    std::string formatted = context.formatted_context;
    if (context.total_tokens_estimate > config_.max_context_tokens) {
        // Truncate context
        int target_chars = config_.max_context_tokens * 4;  // Rough char estimate
        if (formatted.length() > static_cast<size_t>(target_chars)) {
            formatted = formatted.substr(0, target_chars) + "\n...(truncated)\n";
        }
    }

    return formatted + user_message;
}

std::vector<std::string> RAGEngine::getSources() {
    std::vector<std::string> sources;

    if (!initialized_) return sources;

    auto docs = vector_db_->getStats();
    // Get all documents and extract unique sources
    // This is a simplified implementation - could be optimized with a separate sources table

    auto all_docs = vector_db_->getBySource("");  // This won't work, need different approach

    // For now, return empty - would need to implement a sources query in vector_db
    return sources;
}

VectorDBStats RAGEngine::getStats() {
    if (!initialized_) return {};
    return vector_db_->getStats();
}

} // namespace oleg
