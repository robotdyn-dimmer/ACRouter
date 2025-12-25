#include "GitHubOTAChecker.h"
#include <esp_log.h>

static const char* TAG = "GitHubOTA";

GitHubOTAChecker& GitHubOTAChecker::getInstance() {
    static GitHubOTAChecker instance;
    return instance;
}

GitHubOTAChecker::GitHubOTAChecker()
    : _updateAvailable(false)
    , _lastCheckTime(0) {
}

void GitHubOTAChecker::begin(const char* owner, const char* repo, const char* currentVersion) {
    _owner = owner;
    _repo = repo;
    _currentVersion = currentVersion;

    ESP_LOGI(TAG, "Initialized: %s/%s, current version: %s", owner, repo, currentVersion);
}

bool GitHubOTAChecker::checkForUpdate(GitHubRelease& release) {
    // Use cached result if valid
    if (isCacheValid()) {
        ESP_LOGI(TAG, "Using cached release info");
        release = _latestRelease;
        return _updateAvailable;
    }

    // Fetch fresh data
    if (!fetchLatestRelease()) {
        ESP_LOGE(TAG, "Failed to fetch release info");
        return false;
    }

    // Compare versions
    int cmp = compareVersions(_currentVersion.c_str(), _latestRelease.tag_name.c_str());
    _updateAvailable = (cmp < 0); // current < latest

    if (_updateAvailable) {
        ESP_LOGI(TAG, "Update available: %s -> %s",
                 _currentVersion.c_str(), _latestRelease.tag_name.c_str());
    } else if (cmp == 0) {
        ESP_LOGI(TAG, "Already up to date: %s", _currentVersion.c_str());
    } else {
        ESP_LOGI(TAG, "Development version ahead of latest release");
    }

    release = _latestRelease;
    return _updateAvailable;
}

bool GitHubOTAChecker::fetchLatestRelease() {
    HTTPClient http;

    String url = "https://api.github.com/repos/" + _owner + "/" + _repo + "/releases/latest";

    ESP_LOGI(TAG, "Fetching: %s", url.c_str());

    http.begin(url);
    http.addHeader("User-Agent", "ACRouter-OTA-Checker");
    http.addHeader("Accept", "application/vnd.github+json");

    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        ESP_LOGE(TAG, "HTTP GET failed: %d", httpCode);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    // Parse JSON
    if (!parseReleaseJson(payload, _latestRelease)) {
        ESP_LOGE(TAG, "Failed to parse release JSON");
        return false;
    }

    // Update cache timestamp
    _lastCheckTime = millis();

    return true;
}

bool GitHubOTAChecker::parseReleaseJson(const String& json, GitHubRelease& release) {
    DynamicJsonDocument doc(8192); // 8KB buffer

    DeserializationError error = deserializeJson(doc, json);
    if (error) {
        ESP_LOGE(TAG, "JSON parse error: %s", error.c_str());
        return false;
    }

    // Extract fields
    release.tag_name = doc["tag_name"].as<String>();
    release.name = doc["name"].as<String>();
    release.body = doc["body"].as<String>();
    release.published_at = doc["published_at"].as<String>();
    release.is_prerelease = doc["prerelease"].as<bool>();

    // Find .bin asset
    JsonArray assets = doc["assets"];
    for (JsonObject asset : assets) {
        String name = asset["name"].as<String>();
        if (name.endsWith(".bin")) {
            release.asset_name = name;
            release.asset_url = asset["browser_download_url"].as<String>();
            release.asset_size = asset["size"].as<size_t>();
            break;
        }
    }

    if (release.asset_url.isEmpty()) {
        ESP_LOGE(TAG, "No .bin asset found in release");
        return false;
    }

    ESP_LOGI(TAG, "Parsed release: %s (%s, %d bytes)",
             release.tag_name.c_str(), release.asset_name.c_str(), release.asset_size);

    return true;
}

bool GitHubOTAChecker::isCacheValid() const {
    if (_lastCheckTime == 0) return false;
    return (millis() - _lastCheckTime) < CACHE_DURATION_MS;
}

void GitHubOTAChecker::invalidateCache() {
    _lastCheckTime = 0;
}

bool GitHubOTAChecker::forceRefresh() {
    invalidateCache();
    GitHubRelease dummy;
    return checkForUpdate(dummy);
}

/**
 * @brief Compare semantic versions
 * @return -1 if v1 < v2, 0 if equal, 1 if v1 > v2
 */
int GitHubOTAChecker::compareVersions(const char* v1, const char* v2) {
    // Strip 'v' prefix if present
    String s1 = v1;
    String s2 = v2;
    if (s1.startsWith("v")) s1 = s1.substring(1);
    if (s2.startsWith("v")) s2 = s2.substring(1);

    // Handle dev versions (e.g., "1.2.0-dev" > "1.2.0")
    bool v1_dev = s1.indexOf("-dev") != -1;
    bool v2_dev = s2.indexOf("-dev") != -1;

    if (v1_dev) s1 = s1.substring(0, s1.indexOf("-dev"));
    if (v2_dev) s2 = s2.substring(0, s2.indexOf("-dev"));

    // Parse major.minor.patch
    int v1_parts[3] = {0, 0, 0};
    int v2_parts[3] = {0, 0, 0};

    sscanf(s1.c_str(), "%d.%d.%d", &v1_parts[0], &v1_parts[1], &v1_parts[2]);
    sscanf(s2.c_str(), "%d.%d.%d", &v2_parts[0], &v2_parts[1], &v2_parts[2]);

    // Compare parts
    for (int i = 0; i < 3; i++) {
        if (v1_parts[i] < v2_parts[i]) return -1;
        if (v1_parts[i] > v2_parts[i]) return 1;
    }

    // If base versions equal, dev version is "newer"
    if (v1_dev && !v2_dev) return 1;
    if (!v1_dev && v2_dev) return -1;

    return 0; // Equal
}
