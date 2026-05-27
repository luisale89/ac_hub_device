#ifndef CLIO_STRUCTS_H
#define CLIO_STRUCTS_H

// Enum classes
enum LedAnimationStyle
{
  PULSE,
  ALLWAYS_ON,
  ALLWAYS_OFF,
  BLINK,
  BLINK_2X,
  BLINK_05X
};
enum SysModeEnum
{
  AUTO_MODE,
  FAN_MODE,
  COOL_MODE
};
enum SysStateEnum
{
  SYSTEM_ON,
  SYSTEM_OFF,
  SYSTEM_SLEEP,
  SYSTEM_ERROR,
  UNKN
};
enum FlowFlag
{
  FLAG_UP,
  FLAG_DOWN,
  FLAG_UNSET
};
enum SleepWakeCondition
{
  SLEEP_ON_TIME,
  SLEEP_ON_ABSENCE,
  WAKE_ON_TIME,
  WAKE_ON_PRESENCE
};
enum SysFaultEnum
{
  STATUS_UNKN = -1,
  STATUS_OK,
  STATUS_WARNING,
  STATUS_ERROR
};
enum MessageTypeEnum
{
  PAIRING,
  DATA,
};
enum PeerRoleID
{
  SERVER,
  CONTROLLER,
  MONITOR,
  UNSET
};
enum EspNowState
{
  ESPNOW_OFFLINE,
  ESPNOW_ONLINE,
  ESPNOW_IDLE,
};
enum AlarmCode
{
  OFFLINE = -2,
  UNKNOWN_ALARM = -1,
  NORMAL,
  HIGH_DISCHARGE_TEMP,
  HIGH_LIQUID_TEMP,
  HIGH_EXTERIOR_TEMP,
  HIGH_PRESSURE_SWITCH,
  HIGH_COMPRESSOR_CURRENT,
  HIGH_AC_MAINS_VOLTAGE,
  LOW_VAPOR_TEMP,
  LOW_ENTHALPY,
  LOW_PRESSURE_SWITCH,
  LOW_AC_MAINS_VOLTAGE,
  LOW_EVAP_DELTA_T,
  DRAIN_SWITCH_OPEN,
  COMPRESSOR_STALL,
};

typedef struct
{
  bool sleep_control_en;     // habilitación del control sleep.
  bool room_temp_control_en; // habilitación del control de temperatura por sensor de ambiente en la habitación / retorno de aire.
  int comp_nominal_amp;      // corriente nominal del compresor
  int comp_amp_threshold;    // margen de sobrecorriente del compresor.
  int discharge_max_temp;    // temperatura máxima de descarga
  int liquid_max_temp;       // temperatura máxima de líquido
  int vapor_line_min_temp;   // temperatura mínima de vapor.
  int max_recovery_attempts; // numero máximo de intentos de recuperación de una alarma.
  int recovery_window;       // ventana de tiempo para los intentos de recuperación.
  int user_setpoint;         // sp de temperatura del usuario.
  int auto_setpoint;         // sp en modo auto (al detectar ausencia de personas)
  int auto_wait_time;        // tiempo de espera para ajustar el sp en modo auto.
} system_config_struct;

typedef struct
{
  MessageTypeEnum msg_type;   // tipo de mensaje (PAIR)
  PeerRoleID sender_role;     //
  PeerRoleID device_new_role; //
  int channel;                //  - 0 is default, let this value for future changes.
} pairing_data_struct;        //

typedef struct
{
  MessageTypeEnum msg_type;                   //
  PeerRoleID sender_role;                     //
  AlarmCode alarm_code;                       //  alarm_code_state... enum value... alarm detected in the controller device.
  float air_return_temp;                      //  [°C]
  float air_supply_temp;                      //  [°C]
  bool drain_switch;                          //
  bool cooling_relay;                         //
  bool fan_relay;                             //
  unsigned int seconds_since_last_cooling_rq; //  seconds since last false->true relay change.
} controller_data_struct;                     //

typedef struct
{
  MessageTypeEnum msg_type;          //
  PeerRoleID sender_role;            //
  AlarmCode alarm_code;              //  alarm_code_state... enum value... alarm detected in the monitor device.
  float ambient_temp;                //  ambient temperature readings [°C]
  float discharge_temp;              //  discharge temperature readings [°C]
  float liquid_temp;                 //  liquid line temperature readings [°C]
  float vapor_temp;                  //  vapor line temperature readings [°C]
  float low_pressure;                //  low pressure readings [volts, 0-5V]
  float high_pressure;               //  liquid line pressure [volts, 0-5V]
  float ac_mains_voltage;            //  ac mains voltage readings [volts, 90 - 260V]
  bool compressor_state;             //  compressor on|off state (calculated from current value).
  bool compressor_ctrl_signal;       //  indicates the presence of the control signal that turn on the comp.
  float compressor_current;          //  compressor_current readings [volts, 0-1V]
  int compressor_startup_ms;         // time to start the compressor (from 0 amp, peak, < threshold_current_value);
  int seconds_since_last_cooling_rq; //  seconds since last false->true compressor state change.
  int total_cooling_hours;           //  total cooling hours. state in monitor device.
} monitor_data_struct;               //

typedef struct
{
  unsigned long first_fault_time;
  unsigned long last_fault_time;
  int fault_recovery_attempts;
} incident_struct;

typedef struct
{
  AlarmCode controller_ac;
  AlarmCode monitor_ac;
} system_alarms_struct;

#endif