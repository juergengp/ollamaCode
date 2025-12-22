#include "search_client.h"
#include "json.hpp"
#include <curl/curl.h>
#include <regex>
#include <sstream>
#include <set>
#include <iostream>
#include <algorithm>

using json = nlohmann::json;

namespace oleg {

// CURL write callback
static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}

// URL encode helper
static std::string urlEncode(const std::string& str) {
    CURL* curl = curl_easy_init();
    if (!curl) return str;

    char* encoded = curl_easy_escape(curl, str.c_str(), static_cast<int>(str.length()));
    std::string result(encoded);
    curl_free(encoded);
    curl_easy_cleanup(curl);
    return result;
}

// ============================================================================
// DuckDuckGoProvider Implementation
// ============================================================================

DuckDuckGoProvider::DuckDuckGoProvider()
    : userAgent_("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36") {
}

std::vector<SearchResult> DuckDuckGoProvider::search(const std::string& query, int max_results) {
    std::vector<SearchResult> results;

    // DuckDuckGo HTML search (more reliable than API for full results)
    std::string url = "https://html.duckduckgo.com/html/?q=" + urlEncode(query);

    CURL* curl = curl_easy_init();
    if (!curl) return results;

    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent_.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return results;
    }

    // Parse HTML results
    // DuckDuckGo HTML format: <a class="result__a" href="...">title</a>
    // <a class="result__snippet">snippet</a>

    std::regex result_regex(R"REGEX(<a[^>]*class="result__a"[^>]*href="([^"]*)"[^>]*>([^<]*)</a>)REGEX");
    std::regex snippet_regex(R"REGEX(<a[^>]*class="result__snippet"[^>]*>([^<]*)</a>)REGEX");

    auto result_begin = std::sregex_iterator(response.begin(), response.end(), result_regex);
    auto result_end = std::sregex_iterator();

    int count = 0;
    for (auto it = result_begin; it != result_end && count < max_results; ++it, ++count) {
        std::smatch match = *it;
        SearchResult sr;
        sr.url = match[1].str();
        sr.title = match[2].str();
        sr.source = "duckduckgo";

        // Try to find corresponding snippet
        // This is a simplified approach - real implementation would need proper HTML parsing
        sr.snippet = "";

        // Decode URL if it's a redirect
        if (sr.url.find("//duckduckgo.com/l/?") != std::string::npos) {
            std::regex url_regex(R"(uddg=([^&]+))");
            std::smatch url_match;
            if (std::regex_search(sr.url, url_match, url_regex)) {
                CURL* curl2 = curl_easy_init();
                if (curl2) {
                    int out_len;
                    char* decoded = curl_easy_unescape(curl2, url_match[1].str().c_str(), 0, &out_len);
                    if (decoded) {
                        sr.url = std::string(decoded, out_len);
                        curl_free(decoded);
                    }
                    curl_easy_cleanup(curl2);
                }
            }
        }

        results.push_back(sr);
    }

    return results;
}

// ============================================================================
// BraveProvider Implementation
// ============================================================================

BraveProvider::BraveProvider(const std::string& api_key) : api_key_(api_key) {
}

std::vector<SearchResult> BraveProvider::search(const std::string& query, int max_results) {
    std::vector<SearchResult> results;

    if (api_key_.empty()) {
        return results;
    }

    std::string url = "https://api.search.brave.com/res/v1/web/search?q=" + urlEncode(query) +
                      "&count=" + std::to_string(max_results);

    CURL* curl = curl_easy_init();
    if (!curl) return results;

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("X-Subscription-Token: " + api_key_).c_str());
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return results;
    }

    try {
        json data = json::parse(response);

        if (data.contains("web") && data["web"].contains("results")) {
            for (const auto& item : data["web"]["results"]) {
                SearchResult sr;
                sr.title = item.value("title", "");
                sr.url = item.value("url", "");
                sr.snippet = item.value("description", "");
                sr.source = "brave";
                results.push_back(sr);

                if (static_cast<int>(results.size()) >= max_results) break;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Brave search parse error: " << e.what() << std::endl;
    }

    return results;
}

// ============================================================================
// WebSpider Implementation
// ============================================================================

WebSpider::WebSpider()
    : userAgent_("OlEg Spider/1.0")
    , timeout_ms_(30000) {
}

void WebSpider::setUserAgent(const std::string& agent) {
    userAgent_ = agent;
}

void WebSpider::setTimeout(int timeout_ms) {
    timeout_ms_ = timeout_ms;
}

void WebSpider::setProgressCallback(std::function<void(const std::string&, int, int)> callback) {
    progress_callback_ = callback;
}

WebPage WebSpider::fetch(const std::string& url) {
    WebPage page;
    page.url = url;
    page.success = false;

    CURL* curl = curl_easy_init();
    if (!curl) {
        page.error = "Failed to initialize CURL";
        return page;
    }

    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent_.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms_);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        page.error = curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        return page;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (http_code >= 400) {
        page.error = "HTTP error: " + std::to_string(http_code);
        return page;
    }

    page.html = response;
    page.content = htmlToText(response);
    page.links = extractLinks(response, url);

    // Extract title
    std::regex title_regex(R"REGEX(<title[^>]*>([^<]*)</title>)REGEX", std::regex::icase);
    std::smatch match;
    if (std::regex_search(response, match, title_regex)) {
        page.title = match[1].str();
    }

    page.success = true;
    return page;
}

std::vector<WebPage> WebSpider::crawl(const std::string& start_url, int max_pages, int max_depth) {
    std::vector<WebPage> pages;
    std::set<std::string> visited;
    std::vector<std::pair<std::string, int>> queue;  // (url, depth)

    queue.push_back({start_url, 0});

    while (!queue.empty() && static_cast<int>(pages.size()) < max_pages) {
        auto [url, depth] = queue.front();
        queue.erase(queue.begin());

        if (visited.count(url) || depth > max_depth) {
            continue;
        }

        visited.insert(url);

        if (progress_callback_) {
            progress_callback_(url, static_cast<int>(pages.size()), max_pages);
        }

        WebPage page = fetch(url);
        pages.push_back(page);

        if (page.success && depth < max_depth) {
            for (const auto& link : page.links) {
                if (!visited.count(link)) {
                    queue.push_back({link, depth + 1});
                }
            }
        }
    }

    return pages;
}

std::string WebSpider::htmlToText(const std::string& html) {
    std::string text = html;

    // Remove script and style tags with content
    std::regex script_regex(R"REGEX(<script[^>]*>[\s\S]*?</script>)REGEX", std::regex::icase);
    std::regex style_regex(R"REGEX(<style[^>]*>[\s\S]*?</style>)REGEX", std::regex::icase);
    text = std::regex_replace(text, script_regex, "");
    text = std::regex_replace(text, style_regex, "");

    // Remove all HTML tags
    std::regex tag_regex(R"REGEX(<[^>]+>)REGEX");
    text = std::regex_replace(text, tag_regex, " ");

    // Decode common HTML entities
    std::regex nbsp_regex(R"(&nbsp;)");
    std::regex amp_regex(R"(&amp;)");
    std::regex lt_regex(R"(&lt;)");
    std::regex gt_regex(R"(&gt;)");
    std::regex quot_regex(R"(&quot;)");

    text = std::regex_replace(text, nbsp_regex, " ");
    text = std::regex_replace(text, amp_regex, "&");
    text = std::regex_replace(text, lt_regex, "<");
    text = std::regex_replace(text, gt_regex, ">");
    text = std::regex_replace(text, quot_regex, "\"");

    // Normalize whitespace
    std::regex ws_regex(R"(\s+)");
    text = std::regex_replace(text, ws_regex, " ");

    // Trim
    size_t start = text.find_first_not_of(" \t\n\r");
    size_t end = text.find_last_not_of(" \t\n\r");
    if (start != std::string::npos && end != std::string::npos) {
        text = text.substr(start, end - start + 1);
    }

    return text;
}

std::vector<std::string> WebSpider::extractLinks(const std::string& html, const std::string& base_url) {
    std::vector<std::string> links;
    std::set<std::string> unique_links;

    std::regex link_regex(R"REGEX(<a[^>]*href="([^"]*)"[^>]*>)REGEX", std::regex::icase);

    auto begin = std::sregex_iterator(html.begin(), html.end(), link_regex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        std::string href = (*it)[1].str();
        std::string normalized = normalizeUrl(href, base_url);

        if (!normalized.empty() && !unique_links.count(normalized)) {
            unique_links.insert(normalized);
            links.push_back(normalized);
        }
    }

    return links;
}

std::string WebSpider::normalizeUrl(const std::string& url, const std::string& base_url) {
    if (url.empty() || url[0] == '#' || url.find("javascript:") == 0 || url.find("mailto:") == 0) {
        return "";
    }

    // Already absolute
    if (url.find("http://") == 0 || url.find("https://") == 0) {
        return url;
    }

    // Parse base URL
    std::regex url_regex(R"((https?://[^/]+)(/.*)?)");
    std::smatch match;
    if (!std::regex_match(base_url, match, url_regex)) {
        return "";
    }

    std::string base_host = match[1].str();
    std::string base_path = match[2].str();
    if (base_path.empty()) base_path = "/";

    // Protocol-relative URL
    if (url.find("//") == 0) {
        return "https:" + url;
    }

    // Absolute path
    if (url[0] == '/') {
        return base_host + url;
    }

    // Relative path
    size_t last_slash = base_path.rfind('/');
    if (last_slash != std::string::npos) {
        return base_host + base_path.substr(0, last_slash + 1) + url;
    }

    return base_host + "/" + url;
}

// ============================================================================
// SearchClient Implementation
// ============================================================================

SearchClient::SearchClient()
    : current_provider_("duckduckgo") {
    duckduckgo_ = std::make_unique<DuckDuckGoProvider>();
    spider_ = std::make_unique<WebSpider>();
}

void SearchClient::setProvider(const std::string& provider) {
    current_provider_ = provider;
}

void SearchClient::setBraveApiKey(const std::string& key) {
    brave_api_key_ = key;
    if (!key.empty()) {
        brave_ = std::make_unique<BraveProvider>(key);
    }
}

std::vector<std::string> SearchClient::getAvailableProviders() const {
    std::vector<std::string> providers = {"duckduckgo", "spider"};
    if (!brave_api_key_.empty()) {
        providers.push_back("brave");
    }
    return providers;
}

SearchProvider* SearchClient::getProvider(const std::string& name) {
    if (name == "duckduckgo") {
        return duckduckgo_.get();
    } else if (name == "brave" && brave_) {
        return brave_.get();
    }
    return nullptr;
}

std::vector<SearchResult> SearchClient::search(const std::string& query, int max_results) {
    return searchWith(current_provider_, query, max_results);
}

std::vector<SearchResult> SearchClient::searchWith(const std::string& provider, const std::string& query, int max_results) {
    // Spider mode: fetch the query as a URL
    if (provider == "spider") {
        WebPage page = spider_->fetch(query);
        std::vector<SearchResult> results;
        if (page.success) {
            SearchResult sr;
            sr.title = page.title;
            sr.url = page.url;
            sr.snippet = page.content.substr(0, 500);
            sr.source = "spider";
            results.push_back(sr);
        }
        return results;
    }

    SearchProvider* p = getProvider(provider);
    if (p) {
        return p->search(query, max_results);
    }

    // Fallback to duckduckgo
    return duckduckgo_->search(query, max_results);
}

WebPage SearchClient::fetchPage(const std::string& url) {
    return spider_->fetch(url);
}

std::vector<WebPage> SearchClient::crawlSite(const std::string& url, int max_pages) {
    return spider_->crawl(url, max_pages);
}

} // namespace oleg
