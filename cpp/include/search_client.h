#ifndef OLLAMACODE_SEARCH_CLIENT_H
#define OLLAMACODE_SEARCH_CLIENT_H

#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace ollamacode {

// Search result structure
struct SearchResult {
    std::string title;
    std::string url;
    std::string snippet;
    std::string source;  // "duckduckgo", "brave", "spider"
};

// Web page content structure (for spider)
struct WebPage {
    std::string url;
    std::string title;
    std::string content;      // Plain text content
    std::string html;         // Raw HTML (optional)
    std::vector<std::string> links;
    bool success;
    std::string error;
};

// Search provider interface
class SearchProvider {
public:
    virtual ~SearchProvider() = default;
    virtual std::vector<SearchResult> search(const std::string& query, int max_results = 10) = 0;
    virtual std::string getName() const = 0;
};

// DuckDuckGo search provider (no API key required)
class DuckDuckGoProvider : public SearchProvider {
public:
    DuckDuckGoProvider();
    ~DuckDuckGoProvider() override = default;

    std::vector<SearchResult> search(const std::string& query, int max_results = 10) override;
    std::string getName() const override { return "duckduckgo"; }

private:
    std::string userAgent_;
};

// Brave search provider (requires API key)
class BraveProvider : public SearchProvider {
public:
    explicit BraveProvider(const std::string& api_key);
    ~BraveProvider() override = default;

    std::vector<SearchResult> search(const std::string& query, int max_results = 10) override;
    std::string getName() const override { return "brave"; }

private:
    std::string api_key_;
};

// Web spider for fetching and parsing web pages
class WebSpider {
public:
    WebSpider();
    ~WebSpider() = default;

    // Fetch a single page
    WebPage fetch(const std::string& url);

    // Fetch multiple pages (breadth-first crawl)
    std::vector<WebPage> crawl(const std::string& start_url, int max_pages = 10, int max_depth = 2);

    // Set custom user agent
    void setUserAgent(const std::string& agent);

    // Set request timeout (milliseconds)
    void setTimeout(int timeout_ms);

    // Set callback for progress updates
    void setProgressCallback(std::function<void(const std::string&, int, int)> callback);

private:
    std::string userAgent_;
    int timeout_ms_;
    std::function<void(const std::string&, int, int)> progress_callback_;

    // Helper to extract text from HTML
    std::string htmlToText(const std::string& html);

    // Helper to extract links from HTML
    std::vector<std::string> extractLinks(const std::string& html, const std::string& base_url);

    // Helper to normalize URLs
    std::string normalizeUrl(const std::string& url, const std::string& base_url);
};

// Main search client that combines all providers
class SearchClient {
public:
    SearchClient();
    ~SearchClient() = default;

    // Search using configured provider
    std::vector<SearchResult> search(const std::string& query, int max_results = 10);

    // Search using specific provider
    std::vector<SearchResult> searchWith(const std::string& provider, const std::string& query, int max_results = 10);

    // Fetch web page content
    WebPage fetchPage(const std::string& url);

    // Crawl website
    std::vector<WebPage> crawlSite(const std::string& url, int max_pages = 10);

    // Configure providers
    void setProvider(const std::string& provider);
    void setBraveApiKey(const std::string& key);

    // Get available providers
    std::vector<std::string> getAvailableProviders() const;

    // Get current provider
    std::string getCurrentProvider() const { return current_provider_; }

private:
    std::string current_provider_;
    std::string brave_api_key_;

    std::unique_ptr<DuckDuckGoProvider> duckduckgo_;
    std::unique_ptr<BraveProvider> brave_;
    std::unique_ptr<WebSpider> spider_;

    SearchProvider* getProvider(const std::string& name);
};

} // namespace ollamacode

#endif // OLLAMACODE_SEARCH_CLIENT_H
