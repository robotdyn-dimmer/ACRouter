#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

/**
 * @brief GitHub Release information
 */
struct GitHubRelease {
    String tag_name;           // e.g., "v1.2.0"
    String name;               // Release title
    String body;               // Changelog (markdown)
    String published_at;       // ISO 8601 timestamp
    String asset_name;         // Firmware filename
    String asset_url;          // Download URL
    size_t asset_size;         // File size in bytes
    bool is_prerelease;        // true if pre-release
};

/**
 * @brief GitHub OTA Checker
 *
 * Checks GitHub Releases for new firmware versions.
 * Caches results for 5 minutes to avoid API rate limits.
 */
class GitHubOTAChecker {
public:
    static GitHubOTAChecker& getInstance();

    // Configuration
    void begin(const char* owner, const char* repo, const char* currentVersion);

    // Check for updates
    bool checkForUpdate(GitHubRelease& release);
    bool isUpdateAvailable() const { return _updateAvailable; }

    // Get version info
    const char* getCurrentVersion() const { return _currentVersion.c_str(); }

    // Get cached release info
    const GitHubRelease& getLatestRelease() const { return _latestRelease; }

    // Force refresh (bypass cache)
    bool forceRefresh();

    // Version comparison
    static int compareVersions(const char* v1, const char* v2);

private:
    GitHubOTAChecker();
    ~GitHubOTAChecker() = default;
    GitHubOTAChecker(const GitHubOTAChecker&) = delete;
    GitHubOTAChecker& operator=(const GitHubOTAChecker&) = delete;

    // Fetch from GitHub API
    bool fetchLatestRelease();
    bool parseReleaseJson(const String& json, GitHubRelease& release);

    // Cache management
    bool isCacheValid() const;
    void invalidateCache();

    String _owner;
    String _repo;
    String _currentVersion;

    GitHubRelease _latestRelease;
    bool _updateAvailable;
    unsigned long _lastCheckTime;
    static const unsigned long CACHE_DURATION_MS = 300000; // 5 minutes
};
