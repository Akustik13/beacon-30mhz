// Opcodes — mirrors cmd_layer.h. Do NOT renumber.

// Binary command result codes
const int cmdOk             = 0x00;
const int cmdErrLen         = 0x01;
const int cmdErrCrc         = 0x02;
const int cmdErrUnsupported = 0x03;
const int cmdErrBusy        = 0x04;
const int cmdErrState       = 0x05;
const int cmdErrParam       = 0x06;
const int cmdErrFlash       = 0x07;

// Opcodes
const int opPing            = 0x00;
const int opTimeGet         = 0x01;
const int opTimeSet         = 0x02;
const int opReboot          = 0x03;

const int opConfigInfo      = 0x10;
const int opConfigRead      = 0x11;
const int opConfigWrite     = 0x12;
const int opConfigCommit    = 0x13;
const int opConfigReset     = 0x14;

const int opLogInfo         = 0x20;
const int opLogRead         = 0x21;
const int opLogErase        = 0x22;
const int opLogMark         = 0x23;

const int opSensorList      = 0x30;
const int opSensorEnable    = 0x31;
const int opSensorInterval  = 0x32;
const int opSensorReadNow   = 0x33;
const int opAccelCfgGet     = 0x34;
const int opAccelCfgSet     = 0x35;
const int opAccelPower      = 0x36;
const int opAccelProbe      = 0x37;

const int opTxGet           = 0x40;
const int opTxSet           = 0x41;
const int opTxScheduleGet   = 0x42;
const int opTxScheduleSet   = 0x43;

const int opWakeCfgGet      = 0x50;
const int opWakeCfgSet      = 0x51;
const int opWakeStatus      = 0x52;
const int opWakeClear       = 0x53;

const int opHwdescGet       = 0x60;
const int opHwdescSet       = 0x61;
const int opHwdescCommit    = 0x62;
const int hwdescBlobSize    = 128;

// OP_UART_MODE: silent/verbose mode selector — no-op over BLE
const int opUartMode        = 0x06;

const int opBleGet          = 0x70;
const int opBleSet          = 0x71;
const int opBleRssi         = 0x72;
const int opMeasureAll      = 0x73;
const int bleCmdPayloadSz   = 22;

// BLE operating mode bitmask (mirrors app_ble.h)
const int bleOpOff          = 0x00;
const int bleOpContinuous   = 0x01;
const int bleOpSchedule     = 0x02;
const int bleOpGekon        = 0x04;

// Sensor IDs
const int sensorIdTemp      = 0;
const int sensorIdBatt      = 1;
const int sensorIdLight     = 2;
const int sensorIdAccel     = 3;

// Wake action bitmask
const int wakeActionWakeMcu   = 0x01;
const int wakeActionTxBurst   = 0x02;
const int wakeActionLogMarker = 0x04;

// Nordic UART Service UUIDs
const String nusServiceUuid = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const String nusRxCharUuid  = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'; // write: phone → MCU
const String nusTxCharUuid  = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'; // notify: MCU → phone

// Log constants
const int logEntriesMax   = 768;
const int logEntrySize    = 16;
const int flashPageSize   = 2048;
const int protoVersion    = 1;

// LogRecord v2 type bytes
const int logrecTypeTemp    = 0x00;
const int logrecTypeBatt    = 0x01;
const int logrecTypeLight   = 0x02;
const int logrecTypeAccel   = 0x03;
const int logrecTypeMarker  = 0xFE;
const int logrecTypeEmpty   = 0xFF;

// LogRecord v1 flags
const int recFlagFull       = 0x00;
const int recFlagDelta      = 0x01;
const int recFlagCheckpoint = 0x02;
const int recFlagEmpty      = 0xFF;

// log_mask bits
const int logMaskTemp       = 0x01;
const int logMaskLight      = 0x02;
const int logMaskBattPct    = 0x04;
const int logMaskBattMv     = 0x08;

// Event rule engine opcodes (0x80–0x82)
const int opEventGet        = 0x80; // → 84 bytes (7 × 12)
const int opEventSet        = 0x81; // index(1) + event(12) → ok
const int opEventClear      = 0x82; // index(1), 0xFF=all → ok
const int eventBlobSize     = 84;   // 7 events × 12 bytes
