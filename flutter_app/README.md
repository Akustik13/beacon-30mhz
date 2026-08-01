# Beacon BLE Test

Flutter Android test app for the current `BLE_HR_p2p_Sensor` STM32WB1M firmware.

Firmware GATT notes:

- Advertised/GAP device name: `BCN_TEST`; legacy advertising payload currently exposes short name `BCN_`.
- P2P service: `0000fe40-cc7a-482a-984a-7f2ed5b3e58f`
- Sensor service: `00000000-0001-11e1-9ab4-0002a5d5c51b`
- Temperature ENV characteristic: `00040000-0001-11e1-ac36-0002a5d5c51b`

ENV packet format:

```text
[0..1] uint16 little-endian timestamp = HAL_GetTick() >> 3
[2..3] int16 little-endian temperature in 0.1 C
```

Build after Flutter SDK is installed:

```powershell
cd C:\Users\prymv\STM32CubeIDE\workspace_1.19.0\Beacon_30MHz\flutter_app
flutter create . --platforms android
flutter pub get
flutter build apk --release
```
