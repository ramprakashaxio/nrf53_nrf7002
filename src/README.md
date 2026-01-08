# Project Structure

This project has been reorganized into a modular architecture for better maintainability and future integration.

## Directory Structure

```
src/
├── main.c                 # Main entry point & system coordinator
├── wifi/                  # WiFi connectivity module
│   ├── wifi_sta.c        # WiFi station functionality
│   ├── wifi_sta.h
│   ├── http_app.c        # HTTP client for cloud API
│   └── http_app.h
├── sensors/              # Sensor management (placeholder)
│   └── sensor_fsm.h      # Sensor FSM interface
├── ble/                  # BLE connectivity (placeholder)
│   └── ble_service.h     # BLE service interface
└── common/               # Shared utilities
    └── data_manager.h    # Data management interface
```

## Module Responsibilities

### Main Module (`main.c`)
- System initialization
- Thread coordination
- Subsystem lifecycle management
- System monitoring

### WiFi Module (`src/wifi/`)
- WiFi station connectivity
- DHCP client
- HTTP/HTTPS client
- Cloud API communication
- Current endpoint: `hopewatch.onrender.com/api/patient-data`

### Sensor Module (`src/sensors/`) - Placeholder
- Sensor FSM (Finite State Machine)
- Sensor data acquisition
- Data preprocessing
- Ready for MAX30102, ICM42688, LIS2DH12 integration

### BLE Module (`src/ble/`) - Placeholder
- BLE GATT service
- Advertising
- Data notification to mobile app

### Common Module (`src/common/`) - Placeholder
- Data manager (shared data between subsystems)
- Thread-safe data access
- Callback system for data distribution

## Thread Architecture

```
┌─────────────┐
│ Main Thread │ (System Monitor)
└─────────────┘
       │
       ├── WiFi Thread (Priority 5) → HTTP Client
       │
       ├── Sensor Thread (Priority 6) [Future]
       │
       └── BLE Thread (Priority 7) [Future]
```

## Build Instructions

```bash
# Set environment variables
$env:ZEPHYR_SDK_INSTALL_DIR = "C:\Users\Ramprakash\zephyr-sdk-0.17.0"
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
$env:ZEPHYR_BASE = "C:\ncs\v3.1.0\zephyr"

# Clean and configure
Remove-Item -Recurse -Force build
cmake -GNinja -DBOARD=nrf7002dk/nrf5340/cpuapp -Bbuild .

# Build
cd build
ninja

# Flash
cd C:\ncs\v3.1.0
python -m west flash -d C:\Users\Ramprakash\workspace_5\sta\build
```

## Adding New Modules

To add a new module:

1. Create source files in appropriate directory
2. Update `CMakeLists.txt` to include new sources
3. Add initialization call in `main.c`
4. Define thread if needed using `K_THREAD_DEFINE`

## Data Flow (Future)

```
[Sensors] → [FSM] → [Data Manager] → [WiFi/HTTP] → [Cloud API]
                         ↓
                    [BLE Service] → [Mobile App]
```
