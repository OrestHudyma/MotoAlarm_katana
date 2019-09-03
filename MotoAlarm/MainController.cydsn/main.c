/* ========================================
 *
 * Copyright YOUR COMPANY, THE YEAR
 * All Rights Reserved
 * UNPUBLISHED, LICENSED SOFTWARE.
 *
 * CONFIDENTIAL AND PROPRIETARY INFORMATION
 * WHICH IS THE PROPERTY OF your company.
 *
 * ========================================
*/
#include "project.h"

#define ALARM_STATE_ADDRESS 0
#define DISCHARGE_COUNT_ADDRESS 1

#define LED_status_ARMED 255, 4
#define LED_status_DISARMED 255, 0
#define LED_status_DISCHARGING 50, 25
#define LED_status_VALET_SIGNAL 10, 5
#define LED_status_GLOW 255, 255

#define Turns_DISCHARGING 100, 50
#define Turns_OFF 100, 0

#define TURNS_BLINK_TIME 200
#define SIREN_BEEP_TIME 150
#define SIREN_LONG_BEEP_TIME 1000
#define SHORT_DISCHARGE_TIME 15 //seconds
#define LONG_DISCHARGE_TIME 30  //seconds
#define ADDITIONAL_CMD_WAIT_TIME 10 //seconds
#define POWER_STAB_DELAY 2000
#define MAX_DISCHARGE_COUNT 6
#define SW_GLITCH_FILTER_DELAY 20
#define VALET_MODE_ENTER_TIME 10 //seconds
#define MAIN_CYCLE_PERIOD 200 //miliseconds

#define ONE_SECOND 1000
#define ENGINE_ENABLE 1
#define ENGINE_DISABLE 1

#define ADC_HIGH_SIDE_RESISTANCE 39510
#define ADC_LOW_SIDE_RESISTANCE 2420
#define MAIN_BATT_LOW_VOLTAGE 11500 //mV
#define LOW_VOLTAGE_CHECK_TIME 20 //seconds

/** Alarm state. */
typedef enum
{
    /** Alarm state ARMED */
    ALARM_STATE_ARMED,

    /** Alarm state DISARMED */
    ALARM_STATE_DISARMED,

    /** Alarm state DISCHARGING */
    ALARM_STATE_DISCHARGING,
} alarm_state;

/** Alarm level. */
typedef enum
{
    /** Alarm level low */
    ALARM_LEVEL_LOW,

    /** Alarm level high */
    ALARM_LEVEL_HIGH,
    
    /** Alarm level high */
    ALARM_LEVEL_NONE,
} alarm_level;

/** Sensor status */
typedef enum
{
    /** Sensor ready */
    SENSOR_READY,

    /** Sensor not ready */
    SENSOR_NOT_READY,
} sensor_status;

alarm_state current_alarm_state;
uint8 discharge_time = 0;
uint8 InterruptsState;
uint8 discharge_count = 0;

// Sensor readyness status
sensor_status sensor_NH_status;
sensor_status sensor_NL_status;
sensor_status sensor_ignition_status;

void discharge(alarm_level level);
void arm();
void disarm();
void LED_Status_Config(uint8 period, uint8 compare);
void Turns_Config(uint8 period, uint8 compare);
void turns_blink(uint8 blinks_number);
void siren_beep(uint8 beeps_number);
alarm_level check_sensors();
uint8 validate_sensors();
uint16 GetMainVoltage();
void guard();
void check_voltage();

CY_ISR_PROTO(arm_signal);
CY_ISR_PROTO(disarm_signal);
CY_ISR_PROTO(func_signal);

int main(void)
{
    // Wait for power to stabilize
    CyDelay(POWER_STAB_DELAY);
    
    // Initialization
    CyGlobalIntEnable; /* Enable global interrupts. */        
    CyWdtStart(CYWDT_1024_TICKS, CYWDT_LPMODE_DISABLED);    
    ADC_main_batt_Init();
    
    // Wait for supply voltage to reach nominal
    while(GetMainVoltage() < MAIN_BATT_LOW_VOLTAGE) {}
    
    EEPROM_Start();
    PWM_LED_status_Start();
    PWM_turns_Start();
    
    isr_arm_StartEx(arm_signal);
    isr_disarm_StartEx(disarm_signal);
    isr_func_StartEx(func_signal);
    
    current_alarm_state = EEPROM_ReadByte(ALARM_STATE_ADDRESS);
    discharge_count = EEPROM_ReadByte(DISCHARGE_COUNT_ADDRESS);
    
    if (current_alarm_state != ALARM_STATE_DISARMED)
    {
        Control_Reg_engine_enable_Write(ENGINE_DISABLE);
        discharge(ALARM_LEVEL_HIGH);
    }
    
    for(;;)
    {
        guard();
        check_voltage();
        EEPROM_UpdateTemperature();
        CyWdtClear();
        CyDelay(MAIN_CYCLE_PERIOD);
    }
}

void discharge(alarm_level level)
{
    current_alarm_state = ALARM_STATE_DISCHARGING;
    switch(level)
    {
        case ALARM_LEVEL_LOW:        
            discharge_time = SHORT_DISCHARGE_TIME;
        
        case ALARM_LEVEL_HIGH:        
            discharge_time = LONG_DISCHARGE_TIME;
        
        default:
            break;
    }
    Turns_Config(Turns_DISCHARGING);
    LED_Status_Config(LED_status_DISCHARGING);
    if (discharge_count < MAX_DISCHARGE_COUNT)
    {
        Pin_Siren_Write(1);
    }
    while(discharge_time)
    {
        CyDelay(ONE_SECOND);
        discharge_time--;
        CyWdtClear();
    }
    current_alarm_state = ALARM_STATE_ARMED;
    Pin_Siren_Write(0);
    Turns_Config(Turns_OFF);
    LED_Status_Config(LED_status_ARMED);
    discharge_count++;
    EEPROM_WriteByte(discharge_count, DISCHARGE_COUNT_ADDRESS); 
}

void arm()
{
    cystatus status = CYRET_SUCCESS;
    
    current_alarm_state = ALARM_STATE_ARMED;
    EEPROM_WriteByte(current_alarm_state, ALARM_STATE_ADDRESS); 
    status = validate_sensors();    
    Control_Reg_engine_enable_Write(ENGINE_DISABLE);
    discharge_count = 0;
    siren_beep(1);
    turns_blink(1);
    
    // Beep 3 times in case sensor validation failed
    if (status != CYRET_SUCCESS)
    {
        siren_beep(3);
    }
    
    LED_Status_Config(LED_status_ARMED);
}

void disarm()
{    
    if (current_alarm_state == ALARM_STATE_DISCHARGING)
    {
        Pin_Siren_Write(0);
        discharge_time = 1;
        validate_sensors();
        CyDelay(SIREN_BEEP_TIME);
    }
    else
    {
        current_alarm_state = ALARM_STATE_DISARMED;
        Control_Reg_engine_enable_Write(ENGINE_ENABLE);
        EEPROM_WriteByte(current_alarm_state, ALARM_STATE_ADDRESS);
        LED_Status_Config(LED_status_DISARMED);
    }
    siren_beep(2);
    turns_blink(2);
}

void LED_Status_Config(uint8 period, uint8 compare)
{
    PWM_LED_status_WritePeriod(period);
    PWM_LED_status_WriteCompare(compare);
}

void turns_blink(uint8 blinks_number)
{
    uint8 i;
    CyWdtClear();
    for(i = 0; i < blinks_number; i++)
    {
        Control_Reg_Turns_Write(1);
        CyDelay(TURNS_BLINK_TIME);
        Control_Reg_Turns_Write(0);
        CyDelay(TURNS_BLINK_TIME);
        CyWdtClear();
    }
}

void siren_beep(uint8 beeps_number)
{
    uint8 i;
    CyWdtClear();
    for(i = 0; i < beeps_number; i++)
    {
        Pin_Siren_Write(1);
        CyDelay(SIREN_BEEP_TIME);
        Pin_Siren_Write(0);
        CyDelay(SIREN_BEEP_TIME);
        CyWdtClear();
    }
}

void Turns_Config(uint8 period, uint8 compare)
{
    PWM_turns_WritePeriod(period);
    PWM_turns_WriteCompare(compare);
}

alarm_level check_sensors()
{
    uint8 penetration_count = 0;
    
    // Get sensors info
    if (!Pin_sensor_NH_Read() & (sensor_NH_status == SENSOR_READY)) {penetration_count+=2;}
    if (Pin_sensor_NL_Read() & (sensor_NL_status == SENSOR_READY)) {penetration_count++;}
    if (!Pin_ignition_Read() & (sensor_ignition_status == SENSOR_READY)) {penetration_count+=2;}
    
    // Decide alarm level
    if (penetration_count > 1)
    {
        return ALARM_LEVEL_HIGH;
    }
    else if (penetration_count == 1)
    {
        return ALARM_LEVEL_LOW;
    }
    else return ALARM_LEVEL_NONE;
}

uint8 validate_sensors()
{
    uint8 result = CYRET_SUCCESS;
    sensor_NH_status = SENSOR_READY;
    sensor_NL_status = SENSOR_READY;
    sensor_ignition_status = SENSOR_READY;
    
    if (!Pin_sensor_NH_Read()) 
    {
        sensor_NH_status = SENSOR_NOT_READY;
        result = CYRET_INVALID_STATE;
    }
    if (Pin_sensor_NL_Read()) 
    {
        sensor_NL_status = SENSOR_NOT_READY;
        result = CYRET_INVALID_STATE;
    }
    if (!Pin_ignition_Read()) 
    {
        sensor_ignition_status = SENSOR_NOT_READY;
        result = CYRET_INVALID_STATE;
    }
    return result;
}

uint16 GetMainVoltage()
{
    uint32 result;
    
    ADC_main_batt_Enable();
    ADC_main_batt_StartConvert();
    ADC_main_batt_IsEndConversion(ADC_main_batt_WAIT_FOR_RESULT);
    result = ADC_main_batt_GetResult16();
    ADC_main_batt_Stop();
    result *= (ADC_LOW_SIDE_RESISTANCE + ADC_HIGH_SIDE_RESISTANCE) / ADC_LOW_SIDE_RESISTANCE;
    
    return result;
}

void guard()
{    
    alarm_level sensors_check_result;
    
    if (current_alarm_state == ALARM_STATE_ARMED)
    {
        // Check sensors in critical section to avoid random discharges
        InterruptsState = CyEnterCriticalSection();
        sensors_check_result = check_sensors();
        CyExitCriticalSection(InterruptsState);
        
        if (sensors_check_result != ALARM_LEVEL_NONE)
        {
            discharge(sensors_check_result);
        }
    }
}

void check_voltage()
{
    uint8 check_count = 0;
    while ((GetMainVoltage() < MAIN_BATT_LOW_VOLTAGE) && (check_count <= LOW_VOLTAGE_CHECK_TIME))
    {
        check_count++;
        CyDelay(ONE_SECOND);        
        CyWdtClear();
    }
    if(check_count >= LOW_VOLTAGE_CHECK_TIME)
    {
        siren_beep(1);
        CyPmHibernate();
    }
}

CY_ISR(arm_signal)
{    
    CyDelay(SW_GLITCH_FILTER_DELAY);
    if (Pin_arm_Read())
    {
        if(current_alarm_state != ALARM_STATE_ARMED)
        {            
            arm(); 
        }
        else 
        {
            Pin_Siren_Write(1);
            CyDelay(SIREN_LONG_BEEP_TIME);
            Pin_Siren_Write(0);
            turns_blink(1);
        }
    }
    isr_arm_ClearPending();
    Pin_arm_ClearInterrupt();
}

CY_ISR(disarm_signal)
{       
    CyDelay(SW_GLITCH_FILTER_DELAY);
    if (Pin_disarm_Read())
    {
        disarm();
    }
    isr_disarm_ClearPending();
    Pin_disarm_ClearInterrupt();
}

CY_ISR(func_signal)
{
    CyDelay(SW_GLITCH_FILTER_DELAY);
    if (Pin_func_Read())
    {
        // Turn off active sensors
        sensor_NL_status = SENSOR_NOT_READY;
        siren_beep(3);
    }
    isr_func_ClearPending();
    Pin_func_ClearInterrupt();
}

/* [] END OF FILE */
