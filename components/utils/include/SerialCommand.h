/**
 * @file SerialCommand.h
 * @brief Serial command processor for runtime configuration
 *
 * Processes commands from Serial port to change system parameters.
 * Commands are saved to NVS and applied immediately.
 *
 * Command format: <command> [value]
 * Example: "gain 150" sets control_gain to 150
 */

#ifndef SERIAL_COMMAND_H
#define SERIAL_COMMAND_H

#include <Arduino.h>

// Forward declarations
class ConfigManager;
class RouterController;

/**
 * @brief Serial command processor
 *
 * Singleton class that handles Serial input and executes commands.
 */
class SerialCommand {
public:
    /**
     * @brief Get singleton instance
     */
    static SerialCommand& getInstance();

    // Prevent copying
    SerialCommand(const SerialCommand&) = delete;
    SerialCommand& operator=(const SerialCommand&) = delete;

    /**
     * @brief Initialize command processor
     * @param config Pointer to ConfigManager
     * @param router Pointer to RouterController (optional)
     */
    void begin(ConfigManager* config, RouterController* router = nullptr);

    /**
     * @brief Process incoming serial data (call from loop)
     *
     * Non-blocking. Reads available characters and processes
     * complete lines (terminated by '\n' or '\r').
     */
    void process();

    /**
     * @brief Print help message
     */
    void printHelp();

    /**
     * @brief Print current status
     */
    void printStatus();

private:
    SerialCommand();
    ~SerialCommand() = default;

    /**
     * @brief Execute a complete command line
     * @param line Command string (null-terminated)
     */
    void executeCommand(const char* line);

    /**
     * @brief Parse and execute config command
     * @param cmd Command name
     * @param arg Argument string (may be nullptr)
     * @return true if command was handled
     */
    bool handleConfigCommand(const char* cmd, const char* arg);

    /**
     * @brief Parse and execute router command
     * @param cmd Command name
     * @param arg Argument string (may be nullptr)
     * @return true if command was handled
     */
    bool handleRouterCommand(const char* cmd, const char* arg);

    // State
    ConfigManager* m_config;
    RouterController* m_router;

    // Input buffer
    static constexpr size_t BUFFER_SIZE = 64;
    char m_buffer[BUFFER_SIZE];
    size_t m_buffer_pos;

    static const char* TAG;
};

#endif // SERIAL_COMMAND_H
