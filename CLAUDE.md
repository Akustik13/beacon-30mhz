# Beacon 30MHz — Project Context for Claude Code

## Проект
Радіомаяк 30 МГц для польових досліджень гризунів (імплант щура ~300г).
Розробник: Sevskiy GmbH, Schatzbogen 43, 81829 Munich, Germany.

## Шлях до проекту
```
C:\Users\prymv\STM32CubeIDE\workspace_1.19.0\Beacon_30MHz
```

## Документи в проекті
```
Beacon_30MHz/
├── CLAUDE.md                          ← цей файл
├── pdf/
│   ├── beacon_30MHz_v001.pdf          ← схема маяка (фінальне залізо)
│   └── mb1868-wb1m-e02-schematic.pdf  ← схема eval board (фаза 1)
```

---

## ФАЗА 1 — Відладка на B-WB1M-WPAN1 (MB1868)

### Eval board: STM32WB1M STMod+ expansion board (MB1868 rev E02)

#### Що є на платі
| Компонент | Опис |
|-----------|------|
| U5: STM32WB1M | головний МК (STM32WB1MMCH6TR) |
| U2: ISM330DHCX | 3D акселерометр + гіроскоп, I2C |
| U6: STTS22H | температурний сенсор, I2C |
| U3: M24256 | I2C EEPROM 256Kbit |
| U8: LD39050PU33R | LDO 3.3V для живлення МК |
| LD2: BLUE LED | синій LED на PB0 через R13 (120Ом) |
| LD1: GREEN LED | зелений LED — індикатор живлення 5V |
| B1: User Button | кнопка користувача на PB1 через R1 (1K) |
| B2: Reset Button | кнопка Reset (NRST) |
| CN1: STMod+ | 20-pin інтерфейс розширення |
| CN2: SMA | роз'єм зовнішньої антени (не розпаяний за замовч.) |
| CN3: STDC14 | DEBUG роз'єм для ST-LINK |
| CN4: E5V | зовнішнє живлення 5V |
| JP1: IDD | перемичка для вимірювання струму МК |

#### Пінаут STM32WB1M на eval board (MB1868)
| Сигнал | Пін МК | Опис |
|--------|--------|------|
| **LED** | | |
| LD2 BLUE | **PB0** | через R13 120Ом → GND |
| **Кнопки** | | |
| B1 User | **PB1** | через R1 1K, активний LOW |
| B2 Reset | NRST | апаратний reset |
| **SWD програмування** | | |
| SWDIO | **PA13** | |
| SWCLK | **PA14** | |
| NRST | NRST | |
| VCP_TX | **PA9** | UART для консолі (через SB10→SB20) |
| VCP_RX | **PA10** | UART для консолі |
| **I2C сенсори** | | |
| I2C1_SCL | **PB6** | підтяжки R2+R3 по 2.2кОм до 3V3 ✅ |
| I2C1_SDA | **PB7** | підтяжки R2+R3 по 2.2кОм до 3V3 ✅ |
| **Переривання сенсорів** | | |
| ISM330DHCX INT1 | **PA8** | акселерометр interrupt |
| STTS22H INT | **PA0** | температурний сенсор interrupt |
| **STMod+ розширення** | | |
| STMOD+_SPI1_NSS | PA4 | |
| STMOD+_SPI1_MOSIP | PA7 | |
| STMOD+_SPI1_MISOP | PA6 | |
| STMOD+_SPI1_SCK | PA5 | |
| STMOD+_UART1_TX | PA9 | |
| STMOD+_UART1_RX | PA10 | |
| STMOD+_UART1_RTS | PA12 | |
| STMOD+_UART1_CTS | PA11 | |
| STMOD+_INT | PB8 | |
| STMOD+_RESET | PA1 | |
| STMOD+_ADC | PA3 | ADC1_IN8 |
| STMOD+_GPIO1 | PB2 | |
| STMOD+_GPIO2 | PB5 | |
| STMOD+_GPIO3 | PB4 | |
| STMOD+_GPIO4 | PA2 | |

#### I2C адреси сенсорів на платі
| Сенсор | Read | Write |
|--------|------|-------|
| STTS22H (темп.) | 0x7F | 0x7E |
| ISM330DHCX (IMU) | 0xD5 | 0xD4 |
| M24256 EEPROM | 0xAD | 0xAC |

#### Живлення eval board
- Живиться від 5V через CN4 (E5V) або STMod+ (CN1)
- LDO U8 генерує 3.3V для МК
- JP1 (IDD) — перемичка для вимірювання споживання МК (шунт встановлений)
- SB1 — підключає 3V3_MEMs для сенсорів (перевір що замкнута)

#### Програмування
- Через CN3 (STDC14) підключити ST-LINK з Nucleo WB55
- або окремий STLINK-V3SET
- SWD: PA13 (SWDIO), PA14 (SWCLK), NRST

---

## ФАЗА 2 — Фінальне залізо (beacon_30MHz_v001)

### Схема маяка (файл: pdf/beacon_30MHz_v001.pdf)

#### МК: STM32WB1MMCH6TR
#### Пінаут маяка
| Сигнал | Пін МК | Опис |
|--------|--------|------|
| RF_PWR2 | PA8 | Керування потужністю рівень 2 (High) |
| RF_PWR1 | PA1 | Керування потужністю рівень 1 (Mid) |
| CH1 | PA2 | Канал 1 — ємність до кварцу через R3(4Ом) |
| CH2 | PA6 | Канал 2 — ємність до кварцу через R4(4Ом) |
| RF_TX | PA0 | Вмикання передавача (Q1 PJE8406) |
| I2C_SCL | PB6 | I2C до LIS2DW12 |
| I2C_SDA | PB7 | I2C до LIS2DW12 |
| INT1 | PA12 | Переривання від LIS2DW12 |
| INT2 | PA11 | Переривання від LIS2DW12 |
| ADC | PA5 | Вимірювання напруги батареї (через R6/R8) |
| LED | PB1 | Індикатор через R8 (120Ом) |
| SWDIO | PA13 | SWD програмування |
| SWCLK | PA14 | SWD програмування |
| NRST | NRST | Reset (C14 100нФ фільтр) |
| BOOT0 | BOOT0 | Boot (підтягнутий до GND через R10 10кОм) |

#### Сенсори маяка
- LIS2DW12TR — акселерометр I2C (замість ISM330DHCX на eval board)
- Internal STM32 temp sensor — температура кристалу = ~температура тіла щура
- Internal ADC на PA5 — напруга батареї

#### Передавач 30 МГц
- Q1: PJE8406 (P-MOS) — ключ живлення передавача, керується RF_TX (PA0)
- Q2: MMBT3904T-7-F — НВЧ транзистор кварцового генератора
- Y1: кварц 30.****MHz
- L1: 4.7мкГн (5%)
- C5/C6: 4.7мкФ/10нФ — фільтрація живлення передавача
- C7: 47пФ, C8: 68пФ — резонансний контур
- C3: 5пФ, C4: 10пФ — підстройка
- C13: 30пФ/25V — паралельно до кварцу (базова підстройка)
- R1: 220кОм, R2: 390кОм — дільник бази Q2
- R3: 4Ом (CH1), R4: 4Ом (CH2) — захист GPIO + підключення ємностей каналів
- R11: 2.2кОм, R12: 2.2кОм — резистори колектора
- Антена: 2x Amidon T37-6, 400нГн :: 70пФ @ 30МГц

---

## Архітектура прошивки

### State Machine (повна)
```
STATE_INIT
  └── HAL_Init, SystemClock_Config
  └── MX_GPIO_Init, MX_I2C1_Init, MX_ADC1_Init
  └── IWDG_Init (watchdog)
  └── RTC_Init
  └── Flash_LoadConfig()
  └── якщо конфіг валідний → STATE_CHECK_SCHEDULE
  └── якщо немає → STATE_BLE_ADVERTISING (чекаємо першого налаштування)

STATE_CHECK_SCHEDULE
  └── читає RTC: година, день тижня, місяць, дата
  └── перевіряє active_hours_mask, active_days_mask, active_months_mask
  └── якщо активний → STATE_TX
  └── якщо вікно BLE (кожні N хвилин) → STATE_BLE_ADVERTISING
  └── інакше → STATE_SLEEP

STATE_SLEEP (Stop2, ~0.55 мкА)
  └── LL_C2_PWR_SetPowerMode(LL_PWR_MODE_SHUTDOWN)  ← КРИТИЧНО!
  └── HAL_PWREx_EnterSTOP2Mode()
  └── LPTIM або RTC будить через заданий інтервал
  └── після пробудження → STATE_CHECK_SCHEDULE

STATE_TX
  └── temp = read_internal_temp()  ← ОДРАЗУ після пробудження
  └── якщо temp > THRESHOLD → log_overheat()
  └── GPIO: RF_TX=HIGH, RF_PWRx=HIGH (вибір потужності)
  └── GPIO: CHx=LOW (вибір каналу)
  └── LPTIM керує тривалістю імпульсу (tx_duration_ms)
  └── читає ISM330DHCX/LIS2DW12 → логує активність
  └── вимірює ADC батареї
  └── GPIO: всі RF піни LOW (вимкнути передавач)
  └── Flash_AppendLog(timestamp, temp, activity, battery_mv)
  └── tx_sessions_count++
  └── HAL_IWDG_Refresh()
  └── → STATE_CHECK_SCHEDULE

STATE_BLE_ADVERTISING (30 сек)
  └── вмикає BLE стек CPU2
  └── advertising: device name = "BCN_" + unique_id
  └── якщо коннект → STATE_BLE_CONNECTED
  └── таймаут → CPU2 shutdown → STATE_SLEEP

STATE_BLE_CONNECTED
  └── GATT сервіс: читання/запис характеристик
  └── передає: battery_mv, temp, uptime, activity_log
  └── приймає: нові налаштування конфігу
  └── після дисконнекту:
      └── Flash_SaveConfig()
      └── LL_C2_PWR_SetPowerMode(LL_PWR_MODE_SHUTDOWN)
      └── → STATE_SLEEP
```

### Конфігурація (Flash)
```c
typedef struct {
    // Ідентифікація
    uint32_t unique_id;              // унікальний ID імпланту
    char     animal_tag[16];         // мітка тварини напр. "RAT_001"

    // Передавач
    uint8_t  channel;                // 1=CH1, 2=CH2, 3=обидва
    uint8_t  power_level;            // 0=Low, 1=Mid, 2=High
    uint32_t tx_period_ms;           // період між TX сесіями (мс)
    uint32_t tx_duration_ms;         // тривалість TX імпульсу (мс)
    uint32_t tx_pulse_on_ms;         // тривалість ON в імпульсі
    uint32_t tx_pulse_off_ms;        // тривалість OFF в імпульсі

    // Розклад
    uint32_t active_hours_mask;      // біт N = година N активна (0-23)
    uint8_t  active_days_mask;       // біт 0=Пн ... біт 6=Нд
    uint16_t active_months_mask;     // біт 0=Січ ... біт 11=Груд
    uint32_t active_dates[32];       // конкретні дати YYYYMMDD
    uint8_t  dates_count;

    // BLE налаштування
    uint16_t ble_interval_min;       // інтервал між BLE вікнами (хв)
    uint16_t ble_duration_sec;       // тривалість BLE advertising (сек)

    // Порогові значення
    int8_t   temp_threshold_c;       // поріг температури (°C)

    // Моніторинг (оновлюється МК)
    uint16_t battery_mv;             // остання напруга батареї (мВ)
    uint32_t uptime_hours;           // годин роботи
    uint32_t tx_sessions_count;      // кількість TX сесій

    // Службове
    uint32_t config_version;         // версія структури
    uint32_t config_crc;             // CRC32 для валідації
} BeaconConfig_t;
```

### GATT сервіс (BLE протокол)
```
Primary Service UUID: 0xBEAC0001-...-...-...-............

Characteristics:
  WRITE:
  0xBEAC0010 → channel           (1 байт)
  0xBEAC0011 → power_level       (1 байт: 0/1/2)
  0xBEAC0012 → tx_period_ms      (4 байти)
  0xBEAC0013 → tx_duration_ms    (4 байти)
  0xBEAC0014 → tx_pulse_on_ms    (4 байти)
  0xBEAC0015 → tx_pulse_off_ms   (4 байти)
  0xBEAC0016 → active_hours      (4 байти, bitmask)
  0xBEAC0017 → active_days       (1 байт, bitmask)
  0xBEAC0018 → active_months     (2 байти, bitmask)
  0xBEAC0019 → ble_interval      (2 байти, хвилини)
  0xBEAC001A → animal_tag        (16 байт, string)
  0xBEAC001B → calendar_dates    (до 128 байт)
  0xBEAC001C → temp_threshold    (1 байт)

  READ / NOTIFY:
  0xBEAC0020 → battery_mv        (2 байти, мВ)
  0xBEAC0021 → temperature_c     (1 байт, °C)
  0xBEAC0022 → uptime_hours      (4 байти)
  0xBEAC0023 → tx_sessions       (4 байти)
  0xBEAC0024 → unique_id         (4 байти, read only)
  0xBEAC0025 → activity_log      (до 512 байт, burst read)
  0xBEAC0026 → fw_version        (4 байти, read only)
```

---

## Кроки розробки

### Фаза 1 — Крок 1: LED blink на MB1868
```
Файли: Core/Src/main.c, Beacon_30MHz.ioc
МК: STM32WB1M (WB15CCY всередині)
LED: LD2 BLUE на PB0, через R13 120Ом
Кнопка: B1 на PB1 (INPUT, PULL_UP)
Тактування: HSE 32МГц (внутрішній в модулі), SYSCLK 64МГц
Watchdog: IWDG увімкнений з ~30 сек таймаутом
```

### Фаза 1 — Крок 2: BLE Advertising
```
BLE стек: STM32WB BLE (потребує FUS firmware на CPU2)
Device name: "BCN_TEST"
Advertising interval: 1000 мс
Після старту: LED блимає 1 раз/сек поки advertises
Після коннекту: LED горить постійно
```

### Фаза 1 — Крок 3: Flutter app — керування LED
```
Платформа: Android (APK)
Бібліотека: flutter_blue_plus
Функціонал:
  - Скан → знайти "BCN_TEST"
  - Коннект
  - Кнопка ON/OFF для LED (write characteristic)
  - Відображення стану (notify)
  - Дисконнект
```

### Фаза 1 — Крок 4: Сенсори на eval board
```
ISM330DHCX: I2C адреса 0xD4 (write)
  - wake-on-motion через INT1 (PA8)
  - ODR: 12.5 Hz low power mode
  - Акселерометр ±4g
  - Гіроскоп вимкнений (power-down)

STTS22H: I2C адреса 0x7E (write)
  - одноразове вимірювання при пробудженні
  - INT на PA0

Internal temp sensor:
  - читати через ADC одразу після пробудження
  - калібрувати відносно STTS22H
```

### Фаза 2 — Фінальне залізо
```
- Портування прошивки: LIS2DW12 замість ISM330DHCX
- RF керування: RF_TX, RF_PWR1/2, CH1/CH2
- ADC батареї на PA5
- State machine повністю
- Розклад і календар
- Flash лог
- Повний Flutter додаток
```

---

## Важливі нюанси (НЕ ЗАБУТИ!)

### 1. CPU2 shutdown після BLE — КРИТИЧНО
```c
// Після дисконнекту BLE ОБОВ'ЯЗКОВО:
LL_C2_PWR_SetPowerMode(LL_PWR_MODE_SHUTDOWN);
// Без цього: ~108 мкА замість ~0.55 мкА !!!
```

### 2. BLE Firmware (FUS) на CPU2
Перед першим запуском прошити через STM32CubeProgrammer:
- stm32wb1x_BLE_Stack_full_fw.bin (або light)
- Адреса завантаження: залежить від версії, дивись AN5185

### 3. Температуру читати ОДРАЗУ після пробудження
```c
// Правильно:
HAL_ResumeTick();
temp_raw = ADC_ReadInternalTemp(); // ← перше що робимо!
// ... потім решта роботи
```

### 4. Watchdog обов'язково
```c
// В кожному циклі STATE_TX:
HAL_IWDG_Refresh(&hiwdg);
```

### 5. Flash запис — wear leveling
Не записувати в одне й те саме місце постійно.
Використовувати кілька сторінок по черзі або EEPROM емуляцію від ST.

### 6. I2C підтяжки на MB1868
R2=2.2кОм і R3=2.2кОм вже є на платі → не потрібні зовнішні.

---

## Інструменти
- **IDE**: STM32CubeIDE 1.19.0
- **CubeMX**: вбудований в CubeIDE
- **BLE стек**: STM32WB BLE (CPU2 firmware)
- **Програматор**: ST-LINK з Nucleo WB55 або STLINK-V3SET → CN3 (STDC14)
- **Flutter**: Android додаток (APK)
- **Тест BLE**: nRF Connect (Android) для швидкої перевірки

## Структура файлів проекту
```
Beacon_30MHz/
├── CLAUDE.md
├── pdf/
│   ├── beacon_30MHz_v001.pdf
│   └── mb1868-wb1m-e02-schematic.pdf
├── Beacon_30MHz.ioc           ← CubeMX конфіг
├── Core/
│   ├── Inc/
│   │   ├── main.h
│   │   ├── beacon_config.h
│   │   ├── beacon_fsm.h
│   │   ├── ble_beacon_service.h
│   │   ├── lis2dw12.h          (фаза 2)
│   │   ├── ism330dhcx.h        (фаза 1, eval board)
│   │   ├── stts22h.h           (фаза 1, eval board)
│   │   └── flash_storage.h
│   └── Src/
│       ├── main.c
│       ├── beacon_fsm.c
│       ├── ble_beacon_service.c
│       ├── ism330dhcx.c
│       ├── stts22h.c
│       └── flash_storage.c
├── STM32_WPAN/                ← BLE стек ST (генерується CubeMX)
└── flutter_app/
    ├── pubspec.yaml
    └── lib/
        ├── main.dart
        ├── ble_service.dart
        └── screens/
            ├── scan_screen.dart
            ├── connect_screen.dart
            └── config_screen.dart
```
