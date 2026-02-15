#ifndef CLIO_STRUCTS_H
#define CLIO_STRUCTS_H

// Enum classes
enum LedAnimationStyle {PULSE, ALLWAYS_ON, ALLWAYS_OFF, BLINK, BLINK_2X, BLINK_05X};
enum SysModeEnum {AUTO_MODE, FAN_MODE, COOL_MODE};
enum SysStateEnum {SYSTEM_ON, SYSTEM_OFF, SYSTEM_SLEEP, UNKN};
enum FlowFlag {FLAG_UP, FLAG_DOWN, FLAG_UNSET};
enum SleepWakeCondition {SLEEP_ON_TIME, SLEEP_ON_ABSENCE, WAKE_ON_TIME, WAKE_ON_PRESENCE};
enum SysStatusFlag {STATUS_OK, STATUS_WARNING, STATUS_ERROR};
enum MessageTypeEnum {PAIRING, DATA,};
enum PeerRoleID {SERVER, CONTROLLER, MONITOR, UNSET};
enum EspNowState {ESPNOW_OFFLINE, ESPNOW_ONLINE, ESPNOW_IDLE,};
enum AlarmCode {
  NORMAL,
  HIGH_DISCHARGE_TEMP,
  HIGH_LIQUID_TEMP,
  HIGH_EXTERIOR_TEMP,
  LOW_VAPOR_TEMP,
  HIGH_COMP_CURRENT,
  LOW_AC_MAINS_VOLTAGE,
  HIGH_AC_MAINS_VOLTAGE,
  LOW_PRESSURE_SWITCH,
  HIGH_PRESSURE_SWITCH,
  COMPRESSOR_STALL,
  DRAIN_SWITCH_OPEN
};

typedef struct {
  bool sleep_control_enabled;    // (1 byte)
  int comp_nominal_amp;          // (1 bytes)
  int comp_amp_threshold;      // (4 bytes)
  int discharge_max_t;           // (1 bytes)
  int liquid_max_t;              // (1 bytes)
  int exterior_max_t;            // (1 bytes)
  int vapor_line_min_t;          // (1 bytes)
  bool fault_auto_recovery_en;      // (1 byte)
  int max_recovery_attempts;     // (1 byte)
  int recovery_window;           // (1 byte)
  int user_setpoint;             // (1 byte)
  int auto_setpoint;             // (1 byte)
  int auto_wait_time;            // (1 bytes)
} system_config_struct; // TOTAL = 16 bytes

typedef struct {
  MessageTypeEnum msg_type;     // (1 byte)
  PeerRoleID sender_role;       // (1 byte)
  PeerRoleID device_new_role;   // (1 byte)
  int channel;              // (1 byte) - 0 is default, let this value for future changes.
} pairing_data_struct;          // TOTAL = 9 bytes

typedef struct {
  MessageTypeEnum msg_type;// (1 byte)
  PeerRoleID sender_role;  // (1 byte)
  int fault_code;      // (1 byte)
  float air_return_temp;   // (4 bytes) [°C]
  float air_supply_temp;   // (4 bytes) [°C]
  bool drain_switch;       // (1 byte)
  bool cooling_relay;      // (1 byte)
  bool fan_relay;          // (1 byte)
  unsigned int seconds_since_last_cooling_rq;  // (4 bytes) seconds since last false->true relay change.
  unsigned int total_fan_hours; // (4 bytes) total system running hours. state lives in the controller device.
} controller_data_struct;  // TOTAL = 18 bytes

typedef struct {
  MessageTypeEnum msg_type;     // (1 byte)
  PeerRoleID sender_role;       // (1 byte)
  int fault_code;           // (1 byte) 0=no_fault; 1..255 monitor_fault_codes.
  float ambient_temp;           // (4 bytes) ambient temperature readings [°C]
  float discharge_temp;         // (4 bytes) discharge temperature readings [°C]
  float liquid_temp;            // (4 bytes) liquid line temperature readings [°C]
  float vapor_temp;             // (4 bytes) vapor line temperature readings [°C]
  float low_pressure;           // (4 bytes) low pressure readings [volts, 0-5V]
  float high_pressure;          // (4 bytes) liquid line pressure [volts, 0-5V]
  float ac_mains_voltage;       // (4 bytes) ac mains voltage readinsg [volts, 90 - 260V]
  float compressor_current;     // (4 bytes) compressor_current readings [volts, 0-1V]
  bool compressor_state;        // (1 byte) compressor on|off state.
  AlarmCode alarm_code;         // (1 byte) alarm_code_state... enum value
  unsigned int seconds_since_last_cooling_rq;  // (4 bytes) seconds since last false->true compressor state change.
  unsigned int total_cooling_hours;            // (4 bytes) total cooling hours. state in monitor device.
} monitor_data_struct;          // TOTAL = 46 bytes

typedef struct {
  MessageTypeEnum msg_type;     // (1 byte)
  PeerRoleID sender_role;       // (1 byte)
  SysModeEnum peers_mode;      // (1 byte)
  SysStateEnum system_state;    // (1 byte)
  float system_temp_sp;         // (4 bytes) [°C]
  SysStatusFlag alarm_status;   // (1 byte) - controls if the relays are enabled or not
} espnow_settings_struct;     // TOTAL = 13 bytes

typedef struct {
  AlarmCode fault_code;
  unsigned long first_fault_time;
  unsigned long last_fault_time;
  int fault_recovery_attempts;
} incident_struct;

#endif