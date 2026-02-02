#ifndef BLUETOOTH_MANAGER_H
#define BLUETOOTH_MANAGER_H

#include <cstddef>
#include <cstdint>

class BluetoothManager {
public:
    static BluetoothManager& getInstance();

    bool begin();
    void end();
    bool isAdvertising() const;
    bool isConnected() const;

private:
    BluetoothManager() = default;
    BluetoothManager(const BluetoothManager&) = delete;
    BluetoothManager& operator=(const BluetoothManager&) = delete;

    void processCommand(const char* json);

    static constexpr size_t MAX_CONFIG_SIZE = 1024;
    static constexpr size_t RESPONSE_SIZE = 128;
    static constexpr size_t DEVICE_NAME_SIZE = 24;
};

#endif // BLUETOOTH_MANAGER_H
