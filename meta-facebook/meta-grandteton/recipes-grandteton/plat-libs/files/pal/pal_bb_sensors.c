#include <stdio.h>
#include <syslog.h>
#include <openbmc/libgpio.h>
#include <openbmc/obmc-i2c.h>
#include <openbmc/obmc-sensors.h>
#include "pal.h"

//#define DEBUG
#define FAN_PRESNT_TEMPLATE       "FAN%d_PRESENT"

size_t pal_pwm_cnt = FAN_PWM_CNT;
size_t pal_tach_cnt = FAN_TACH_CNT;
const char pal_pwm_list[] = "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15";
const char pal_tach_list[] = "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31";
//static float InletCalibration = 0;

static int read_bb_temp(uint8_t fru, uint8_t sensor_num, float *value);
static int read_fan_speed(uint8_t fru, uint8_t sensor_num, float *value);
static int read_48v_hsc_vin(uint8_t fru, uint8_t sensor_num, float *value);
static int read_48v_hsc_iout(uint8_t fru, uint8_t sensor_num, float *value);
static int read_48v_hsc_pin(uint8_t fru, uint8_t sensor_num, float *value);
static int read_48v_hsc_peak_pin(uint8_t fru, uint8_t sensor_num, float *value);
static int read_48v_hsc_temp(uint8_t fru, uint8_t sensor_num, float *value);
static int read_brick_vout(uint8_t fru, uint8_t sensor_num, float *value);
static int read_brick_iout(uint8_t fru, uint8_t sensor_num, float *value);
static int read_brick_temp(uint8_t fru, uint8_t sensor_num, float *value);
static int read_nic0_power(uint8_t fru, uint8_t sensor_num, float *value);
static int read_nic1_power(uint8_t fru, uint8_t sensor_num, float *value);

const uint8_t nic0_sensor_list[] = {
  NIC0_SNR_MEZZ0_TEMP,
  NIC0_SNR_MEZZ0_P3V3_VOUT,
  NIC0_SNR_MEZZ0_P12V_IOUT,
  NIC0_SNR_MEZZ0_P12V_POUT,
};

const uint8_t nic1_sensor_list[] = {
  NIC1_SNR_MEZZ1_TEMP,
  NIC1_SNR_MEZZ1_P3V3_VOUT,
  NIC1_SNR_MEZZ1_P12V_IOUT,
  NIC1_SNR_MEZZ1_P12V_POUT,
};

const uint8_t pdbv_sensor_list[] = {
  PDBV_SNR_HSC0_VIN,
  PDBV_SNR_HSC0_IOUT,
  PDBV_SNR_HSC0_PIN,
  PDBV_SNR_HSC0_TEMP,
  PDBV_SNR_BRICK0_VOUT,
  PDBV_SNR_BRICK0_IOUT,
  PDBV_SNR_BRICK0_TEMP,
  PDBV_SNR_BRICK1_VOUT,
  PDBV_SNR_BRICK1_IOUT,
  PDBV_SNR_BRICK1_TEMP,
  PDBV_SNR_BRICK2_VOUT,
  PDBV_SNR_BRICK2_IOUT,
  PDBV_SNR_BRICK2_TEMP,
  PDBV_SNR_ADC128_P3V3_AUX,
};

const uint8_t pdbh_sensor_list[] = {
  PDBH_SNR_HSC1_VIN,
  PDBH_SNR_HSC1_IOUT,
  PDBH_SNR_HSC1_PIN,
  PDBH_SNR_HSC1_TEMP,
  PDBH_SNR_HSC2_VIN,
  PDBH_SNR_HSC2_IOUT,
  PDBH_SNR_HSC2_PIN,
  PDBH_SNR_HSC2_TEMP,
};

const uint8_t bp0_sensor_list[] = {
  BP0_SNR_FAN0_INLET_SPEED,
  BP0_SNR_FAN0_OUTLET_SPEED,
  BP0_SNR_FAN1_INLET_SPEED,
  BP0_SNR_FAN1_OUTLET_SPEED,
  BP0_SNR_FAN4_INLET_SPEED,
  BP0_SNR_FAN4_OUTLET_SPEED,
  BP0_SNR_FAN5_INLET_SPEED,
  BP0_SNR_FAN5_OUTLET_SPEED,
  BP0_SNR_FAN8_INLET_SPEED,
  BP0_SNR_FAN8_OUTLET_SPEED,
  BP0_SNR_FAN9_INLET_SPEED,
  BP0_SNR_FAN9_OUTLET_SPEED,
  BP0_SNR_FAN12_INLET_SPEED,
  BP0_SNR_FAN12_OUTLET_SPEED,
  BP0_SNR_FAN13_INLET_SPEED,
  BP0_SNR_FAN13_OUTLET_SPEED,
};

const uint8_t bp1_sensor_list[] = {
  BP1_SNR_FAN2_INLET_SPEED,
  BP1_SNR_FAN2_OUTLET_SPEED,
  BP1_SNR_FAN3_INLET_SPEED,
  BP1_SNR_FAN3_OUTLET_SPEED,
  BP1_SNR_FAN6_INLET_SPEED,
  BP1_SNR_FAN6_OUTLET_SPEED,
  BP1_SNR_FAN7_INLET_SPEED,
  BP1_SNR_FAN7_OUTLET_SPEED,
  BP1_SNR_FAN10_INLET_SPEED,
  BP1_SNR_FAN10_OUTLET_SPEED,
  BP1_SNR_FAN11_INLET_SPEED,
  BP1_SNR_FAN11_OUTLET_SPEED,
  BP1_SNR_FAN14_INLET_SPEED,
  BP1_SNR_FAN14_OUTLET_SPEED,
  BP1_SNR_FAN15_INLET_SPEED,
  BP1_SNR_FAN15_OUTLET_SPEED,
};

const uint8_t scm_sensor_list[] = {
  SCM_SNR_P12V,
  SCM_SNR_P5V,
  SCM_SNR_P3V3,
  SCM_SNR_P2V5,
  SCM_SNR_P1V8,
  SCM_SNR_PGPPA,
  SCM_SNR_P1V2,
  SCM_SNR_P1V0,
  SCM_SNR_BMC_TEMP,
};


//48V HSC
char *hsc_adm1272_chips[HSC_48V_CNT] = {
    "adm1272-i2c-38-10",
    "adm1272-i2c-39-13",
    "adm1272-i2c-39-1c",
};
char **hsc_48v_chips = hsc_adm1272_chips;


//BRICK
char *brick_pmbus_chips[BRICK_CNT] = {
    "pmbus-i2c-38-69",
    "pmbus-i2c-38-6a",
    "pmbus-i2c-38-6b",
};

//FAN
PAL_I2C_BUS_INFO fan_info_list[] = {
  {FAN_CHIP_ID0, I2C_BUS_40, 0x5E},
  {FAN_CHIP_ID1, I2C_BUS_40, 0x40},
  {FAN_CHIP_ID2, I2C_BUS_41, 0x5E},
  {FAN_CHIP_ID3, I2C_BUS_41, 0x40},
};


//FAN CHIP
char *max31790_chips[] = {
  "max31790-i2c-40-2f",  // BP0
  "max31790-i2c-40-20",  // BP0
  "max31790-i2c-41-2f",  // BP1
  "max31790-i2c-41-20",  // BP1
};
char **fan_chips = max31790_chips;

//{SensorName, ID, FUNCTION, PWR_STATUS, {UCR, UNC, UNR, LCR, LNC, LNR, Pos, Neg}
PAL_SENSOR_MAP bb_sensor_map[] = {
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x00
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x01
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x02
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x03
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x04
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x05
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x06
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x07
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x08
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x09
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x0A
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x0B
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x0C
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x0D
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x0E
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x0F

  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x10
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x11
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x12
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x13
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x14
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x15
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x16
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x17
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x18
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x19
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x1A
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x1B
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x1C
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x1D
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x1E
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x1F

  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x20
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x21
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x22
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x23
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x24
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x25
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x26
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x27
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x28
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x29
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x2A
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x2B
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x2C
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x2D
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x2E
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x2F

  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x30
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x31
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0X32
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x33
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x34
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x35
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0X36
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x37
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x38
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x39
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0X3A
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x3B
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x3C
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x3D
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0X3E
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x3F
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x40
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x41
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0X42
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x43
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x44
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x45
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x46
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x47

  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x48
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x49
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0X4A
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x4B
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x4C
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x4D
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0X4E
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x4F
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x50
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x51
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0X52
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x53
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x54
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x55
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0X56
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x57
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x58
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x59
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0X5A
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x5B
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x5C
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x5D
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x5E
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x5F

  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x60
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x61
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x62
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x63
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x64
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x65
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x66
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x67
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x68
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x69
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x6A
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x6B
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x6C
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x6D
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x6E
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x6F

  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x70
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x71
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x72
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x73
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x74
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x75
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x76
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x77
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x78
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x79
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x7A
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x7B
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x7C
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x7D
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x7E
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x7F

  {"MEZZ0_P3V3_VOLT", DPM_2, read_dpm_vout, false, {0, 0, 0, 0, 0, 0, 0, 0}, VOLT}, //0x80
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x81
  {"MEZZ0_P12V_CURR", ADC128_0, read_adc128_val, false, {0, 0, 0, 0, 0, 0, 0, 0}, CURR}, //0x82
  {"MEZZ0_P12V_PWR", MEZZ0, read_nic0_power, false, {0, 0, 0, 0, 0, 0, 0, 0}, POWER}, //0x83
  {"MEZZ0_TEMP", TEMP_MEZZ0, read_bb_temp, true, {0, 0, 0, 0, 0, 0, 0, 0}, TEMP},  //0x84
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x85
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x86
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x87

  {"MEZZ1_P3V3_VOLT", DPM_3, read_dpm_vout, false, {0, 0, 0, 0, 0, 0, 0, 0}, VOLT}, //0x88
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x89
  {"MEZZ1_P12V_CURR", ADC128_0, read_adc128_val, false, {0, 0, 0, 0, 0, 0, 0, 0}, CURR}, //0x8A
  {"MEZZ1_P12V_PWR", MEZZ1, read_nic1_power, false, {0, 0, 0, 0, 0, 0, 0, 0}, POWER}, //0x8B
  {"MEZZ1_TEMP", TEMP_MEZZ1, read_bb_temp, true, {0, 0, 0, 0, 0, 0, 0, 0}, TEMP},  //0x8C
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x8D
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x8E
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x8F

  {"HSC0_VOLT",     HSC_48V_ID0, read_48v_hsc_vin,      true, {0, 0, 0, 0, 0, 0, 0, 0}, VOLT},  //0x90
  {"HSC0_CURR",     HSC_48V_ID0, read_48v_hsc_iout,     true, {0, 0, 0, 0, 0, 0, 0, 0}, CURR},  //0x91
  {"HSC0_PWR",      HSC_48V_ID0, read_48v_hsc_pin,      true, {0, 0, 0, 0, 0, 0, 0, 0}, POWER}, //0x92
  {"HSC0_TEMP",     HSC_48V_ID0, read_48v_hsc_temp,     true, {0, 0, 0, 0, 0, 0, 0, 0}, TEMP},  //0x93
  {"HSC0_PEAK_PIN", HSC_48V_ID0, read_48v_hsc_peak_pin, true, {0, 0, 0, 0, 0, 0, 0, 0}, POWER}, //0x94

  {"BRICK0_VOLT", BRICK_ID0, read_brick_vout, true, {0, 0, 0, 0, 0, 0, 0, 0}, VOLT}, //0x95
  {"BRICK0_CURR", BRICK_ID0, read_brick_iout, true, {0, 0, 0, 0, 0, 0, 0, 0}, CURR}, //0x96
  {"BRICK0_TEMP", BRICK_ID0, read_brick_temp, true, {0, 0, 0, 0, 0, 0, 0, 0}, TEMP}, //0x97
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x98
  {"BRICK1_VOLT", BRICK_ID1, read_brick_vout, true, {0, 0, 0, 0, 0, 0, 0, 0}, VOLT}, //0x99
  {"BRICK1_CURR", BRICK_ID1, read_brick_iout, true, {0, 0, 0, 0, 0, 0, 0, 0}, CURR}, //0x9A
  {"BRICK1_TEMP", BRICK_ID1, read_brick_temp, true, {0, 0, 0, 0, 0, 0, 0, 0}, TEMP}, //0x9B
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0x9C
  {"BRICK2_VOLT", BRICK_ID2, read_brick_vout, true, {0, 0, 0, 0, 0, 0, 0, 0}, VOLT}, //0x9D
  {"BRICK2_CURR", BRICK_ID2, read_brick_iout, true, {0, 0, 0, 0, 0, 0, 0, 0}, CURR}, //0x9E
  {"BRICK2_TEMP", BRICK_ID2, read_brick_temp, true, {0, 0, 0, 0, 0, 0, 0, 0}, TEMP}, //0x9F

  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xA0
  {"P3V3_AUX_IN6_VOLT", ADC128_0, read_adc128_val, true, {0, 0, 0, 0, 0, 0, 0, 0}, VOLT}, //0xA1
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xA2
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xA3
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xA4
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xA5
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xA6
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xA7
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xA8
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xA9
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xAA
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xAB
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xAC
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xAD
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xAE
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xAF

  {"HSC1_VOLT",     HSC_48V_ID1, read_48v_hsc_vin,      true, {0, 0, 0, 0, 0, 0, 0, 0}, VOLT},  //0xB0
  {"HSC1_CURR",     HSC_48V_ID1, read_48v_hsc_iout,     true, {0, 0, 0, 0, 0, 0, 0, 0}, CURR},  //0xB1
  {"HSC1_PWR",      HSC_48V_ID1, read_48v_hsc_pin,      true, {0, 0, 0, 0, 0, 0, 0, 0}, POWER}, //0xB2
  {"HSC1_TEMP",     HSC_48V_ID1, read_48v_hsc_temp,     true, {0, 0, 0, 0, 0, 0, 0, 0}, TEMP},  //0xB3
  {"HSC1_PEAK_PIN", HSC_48V_ID1, read_48v_hsc_peak_pin, true, {0, 0, 0, 0, 0, 0, 0, 0}, POWER}, //0xB4
  {"HSC2_VOLT",     HSC_48V_ID2, read_48v_hsc_vin,      true, {0, 0, 0, 0, 0, 0, 0, 0}, VOLT},  //0xB5
  {"HSC2_CURR",     HSC_48V_ID2, read_48v_hsc_iout,     true, {0, 0, 0, 0, 0, 0, 0, 0}, CURR},  //0xB6
  {"HSC2_PWR",      HSC_48V_ID2, read_48v_hsc_pin,      true, {0, 0, 0, 0, 0, 0, 0, 0}, POWER}, //0xB7
  {"HSC2_TEMP",     HSC_48V_ID2, read_48v_hsc_temp,     true, {0, 0, 0, 0, 0, 0, 0, 0}, TEMP},  //0xB8
  {"HSC2_PEAK_PIN", HSC_48V_ID2, read_48v_hsc_peak_pin, true, {0, 0, 0, 0, 0, 0, 0, 0}, POWER}, //0xB9

  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xBA
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xBB
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xBC
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xBD
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xBE
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xBF

  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xC0
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xC1
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xC2
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xC3
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xC4
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xC5
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xC6
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xC7
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xC8
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xC9
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xCA
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xCB
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xCC
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xCD
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xCE
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xCF

  {"FAN0_INLET_SPEED",  FAN_TACH_ID0,  read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xD0
  {"FAN0_OUTLET_SPEED", FAN_TACH_ID1,  read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xD1
  {"FAN1_INLET_SPEED",  FAN_TACH_ID2,  read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xD2
  {"FAN1_OUTLET_SPEED", FAN_TACH_ID3,  read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xD3
  {"FAN2_INLET_SPEED",  FAN_TACH_ID4,  read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xD4
  {"FAN2_OUTLET_SPEED", FAN_TACH_ID5,  read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xD5
  {"FAN3_INLET_SPEED",  FAN_TACH_ID6,  read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xD6
  {"FAN3_OUTLET_SPEED", FAN_TACH_ID7,  read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xD7
  {"FAN4_INLET_SPEED",  FAN_TACH_ID8,  read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xD8
  {"FAN4_OUTLET_SPEED", FAN_TACH_ID9,  read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xD9
  {"FAN5_INLET_SPEED",  FAN_TACH_ID10, read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xDA
  {"FAN5_OUTLET_SPEED", FAN_TACH_ID11, read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xDB
  {"FAN6_INLET_SPEED",  FAN_TACH_ID12, read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xDC
  {"FAN6_OUTLET_SPEED", FAN_TACH_ID13, read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xDD
  {"FAN7_INLET_SPEED",  FAN_TACH_ID14, read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xDE
  {"FAN7_OUTLET_SPEED", FAN_TACH_ID15, read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xDF

  {"FAN8_INLET_SPEED",  FAN_TACH_ID16, read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xE0
  {"FAN8_OUTLET_SPEED", FAN_TACH_ID17, read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xE1
  {"FAN9_INLET_SPEED",  FAN_TACH_ID18, read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xE2
  {"FAN9_OUTLET_SPEED", FAN_TACH_ID19, read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xE3
  {"FAN10_INLET_SPEED",  FAN_TACH_ID20, read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xE4
  {"FAN10_OUTLET_SPEED", FAN_TACH_ID21, read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xE5
  {"FAN11_INLET_SPEED",  FAN_TACH_ID22, read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xE6
  {"FAN11_OUTLET_SPEED", FAN_TACH_ID23, read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xE7
  {"FAN12_INLET_SPEED",  FAN_TACH_ID24, read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xE8
  {"FAN12_OUTLET_SPEED", FAN_TACH_ID25, read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xE9
  {"FAN13_INLET_SPEED",  FAN_TACH_ID26, read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xEA
  {"FAN13_OUTLET_SPEED", FAN_TACH_ID27, read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xEB
  {"FAN14_INLET_SPEED",  FAN_TACH_ID28, read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xEC
  {"FAN14_OUTLET_SPEED", FAN_TACH_ID29, read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xED
  {"FAN15_INLET_SPEED",  FAN_TACH_ID30, read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xEE
  {"FAN15_OUTLET_SPEED", FAN_TACH_ID31, read_fan_speed, true, {0, 0, 0, 0, 0, 0, 0, 0}, FAN}, //0xEF

  {"P12V_VOLT",  ADC0, read_adc_val, false, {0, 0, 0, 0, 0, 0, 0, 0}, VOLT}, //0xF0
  {"P5V_VOLT",   ADC1, read_adc_val, false, {0, 0, 0, 0, 0, 0, 0, 0}, VOLT}, //0xF1
  {"P3V3_VOLT",  ADC2, read_adc_val, false, {0, 0, 0, 0, 0, 0, 0, 0}, VOLT}, //0xF2
  {"P2V5_VOLT",  ADC3, read_adc_val, false, {0, 0, 0, 0, 0, 0, 0, 0}, VOLT}, //0xF3
  {"P1V8_VOLT",  ADC4, read_adc_val, false, {0, 0, 0, 0, 0, 0, 0, 0}, VOLT}, //0xF4
  {"PGPPA_VOLT", ADC5, read_adc_val, false, {0, 0, 0, 0, 0, 0, 0, 0}, VOLT}, //0xF5
  {"P1V2_VOLT",  ADC6, read_adc_val, false, {0, 0, 0, 0, 0, 0, 0, 0}, VOLT}, //0xF6
  {"P1V0_VOLT",  ADC8, read_adc_val, false, {0, 0, 0, 0, 0, 0, 0, 0}, VOLT}, //0xF7
  {"BMC_P12V_VOLT", DPM_4, read_dpm_vout, false, {0, 0, 0, 0, 0, 0, 0, 0}, VOLT}, //0xF8
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xF9
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xFA
  {"BMC_TEMP", TEMP_BMC, read_bb_temp, true, {0, 0, 0, 0, 0, 0, 0, 0}, TEMP}, //0xFB
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xFC
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xFD
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xFE
  {NULL, 0, NULL, 0, {0, 0, 0, 0, 0, 0, 0, 0}, 0}, //0xFF
};

extern struct snr_map sensor_map[];

size_t nic0_sensor_cnt = sizeof(nic0_sensor_list)/sizeof(uint8_t);
size_t nic1_sensor_cnt = sizeof(nic1_sensor_list)/sizeof(uint8_t);
size_t pdbv_sensor_cnt = sizeof(pdbv_sensor_list)/sizeof(uint8_t);
size_t pdbh_sensor_cnt = sizeof(pdbh_sensor_list)/sizeof(uint8_t);
size_t bp0_sensor_cnt = sizeof(bp0_sensor_list)/sizeof(uint8_t);
size_t bp1_sensor_cnt = sizeof(bp1_sensor_list)/sizeof(uint8_t);
size_t scm_sensor_cnt = sizeof(scm_sensor_list)/sizeof(uint8_t);

static int
read_nic0_power(uint8_t fru, uint8_t sensor_num, float *value) {
  int ret;

  ret = oper_adc128_power(fru, MB_SNR_HSC_VIN, NIC0_SNR_MEZZ0_P12V_IOUT, value);
  return ret;
}

static int
read_nic1_power(uint8_t fru, uint8_t sensor_num, float *value) {
  int ret;

  ret = oper_adc128_power(fru, MB_SNR_HSC_VIN, NIC1_SNR_MEZZ1_P12V_IOUT, value);
  return ret;
}

static int
read_bb_temp (uint8_t fru, uint8_t sensor_num, float *value) {
  int ret;
  uint8_t snr_id = sensor_map[fru].map[sensor_num].id;

  char *devs[] = {
    "tmp75-i2c-1-4b",
    "tmp421-i2c-13-1f",
    "tmp421-i2c-4-1f",
  };

  if (snr_id >= ARRAY_SIZE(devs)) {
    return -1;
  }

  ret = sensors_read(devs[snr_id], sensor_map[fru].map[sensor_num].snr_name, value);
  return ret;
}

static int
read_48v_hsc_vin(uint8_t fru, uint8_t sensor_num, float *value) {
  int ret;
  uint8_t hsc_48v_id = sensor_map[fru].map[sensor_num].id;
  static int retry[HSC_48V_CNT];


  if (hsc_48v_id >= HSC_48V_CNT)
    return -1;

  ret = sensors_read(hsc_48v_chips[hsc_48v_id], sensor_map[fru].map[sensor_num].snr_name, value);
  if (*value == 0) {
    retry[hsc_48v_id]++;
    return retry_err_handle(retry[hsc_48v_id], 5);
  }

  retry[hsc_48v_id] = 0;
  return ret;
}

static int
read_48v_hsc_iout(uint8_t fru, uint8_t sensor_num, float *value) {
  int ret;
  uint8_t hsc_48v_id = sensor_map[fru].map[sensor_num].id;
  static int retry[HSC_48V_CNT];


  if (hsc_48v_id >= HSC_48V_CNT)
    return -1;

  ret = sensors_read(hsc_48v_chips[hsc_48v_id], sensor_map[fru].map[sensor_num].snr_name, value);
  if (ret) {
    retry[hsc_48v_id]++;
    return retry_err_handle(retry[hsc_48v_id], 5);
  }

  retry[hsc_48v_id] = 0;
  return ret;
}

static int
read_48v_hsc_pin(uint8_t fru, uint8_t sensor_num, float *value) {
  int ret;
  uint8_t hsc_48v_id = sensor_map[fru].map[sensor_num].id;
  static int retry[HSC_48V_CNT];


  if (hsc_48v_id >= HSC_48V_CNT)
    return -1;

  ret = sensors_read(hsc_48v_chips[hsc_48v_id], sensor_map[fru].map[sensor_num].snr_name, value);
  if (ret) {
    retry[hsc_48v_id]++;
    return retry_err_handle(retry[hsc_48v_id], 5);
  }

  retry[hsc_48v_id] = 0;
  return ret;
}

static int
read_48v_hsc_temp(uint8_t fru, uint8_t sensor_num, float *value) {
  int ret;
  uint8_t hsc_48v_id = sensor_map[fru].map[sensor_num].id;
  static int retry[HSC_48V_CNT];


  if (hsc_48v_id >= HSC_48V_CNT)
    return -1;

  ret = sensors_read(hsc_48v_chips[hsc_48v_id], sensor_map[fru].map[sensor_num].snr_name, value);
  if (*value == 0) {
    retry[hsc_48v_id]++;
    return retry_err_handle(retry[hsc_48v_id], 5);
  }

  retry[hsc_48v_id] = 0;
  return ret;
}

static int
read_48v_hsc_peak_pin(uint8_t fru, uint8_t sensor_num, float *value) {
  int ret;
  uint8_t hsc_48v_id = sensor_map[fru].map[sensor_num].id;
  static int retry[HSC_48V_CNT];


  if (hsc_48v_id >= HSC_48V_CNT)
    return -1;

  ret = sensors_read(hsc_48v_chips[hsc_48v_id], sensor_map[fru].map[sensor_num].snr_name, value);
  if (*value == 0) {
    retry[hsc_48v_id]++;
    return retry_err_handle(retry[hsc_48v_id], 5);
  }

  retry[hsc_48v_id] = 0;
  return ret;
}

static int
read_brick_vout(uint8_t fru, uint8_t sensor_num, float *value) {
  int ret;
  uint8_t brick_id = sensor_map[fru].map[sensor_num].id;
  static int retry[BRICK_CNT];


  if (brick_id >= BRICK_CNT)
    return -1;

  ret = sensors_read(brick_pmbus_chips[brick_id], sensor_map[fru].map[sensor_num].snr_name, value);
  if (*value == 0) {
    retry[brick_id]++;
    return retry_err_handle(retry[brick_id], 5);
  }

  retry[brick_id] = 0;
  return ret;
}

static int
read_brick_iout(uint8_t fru, uint8_t sensor_num, float *value) {
  int ret;
  uint8_t brick_id = sensor_map[fru].map[sensor_num].id;
  static int retry[BRICK_CNT];


  if (brick_id >= BRICK_CNT)
    return -1;

  ret = sensors_read(brick_pmbus_chips[brick_id], sensor_map[fru].map[sensor_num].snr_name, value);
  if (*value == 0) {
    retry[brick_id]++;
    return retry_err_handle(retry[brick_id], 5);
  }

  retry[brick_id] = 0;
  return ret;
}

static int
read_brick_temp(uint8_t fru, uint8_t sensor_num, float *value) {
  int ret;
  uint8_t brick_id = sensor_map[fru].map[sensor_num].id;
  static int retry[BRICK_CNT];


  if (brick_id >= BRICK_CNT)
    return -1;

  ret = sensors_read(brick_pmbus_chips[brick_id], sensor_map[fru].map[sensor_num].snr_name, value);
  if (*value == 0) {
    retry[brick_id]++;
    return retry_err_handle(retry[brick_id], 5);
  }

  retry[brick_id] = 0;
  return ret;
}

static bool
is_fan_present(int fan_num) {
  int ret = 0;
  gpio_desc_t *gdesc;
  gpio_value_t val;
  char shadow[20] = {0};

  sprintf(shadow, FAN_PRESNT_TEMPLATE, fan_num);

  gdesc = gpio_open_by_shadow(shadow);
  if (gdesc) {
    if (gpio_get_value(gdesc, &val) < 0) {
      syslog(LOG_WARNING, "Get GPIO %s failed", shadow);
      val = GPIO_VALUE_INVALID;
    }
    ret |= (val == GPIO_VALUE_LOW)? 1: 0;
    gpio_close(gdesc);
  }

  return ret? true: false;
}

static int
read_fan_speed(uint8_t fru, uint8_t sensor_num, float *value) {
  int ret = 0;
  uint8_t tach_id = sensor_map[fru].map[sensor_num].id;
  static uint8_t retry[FAN_TACH_CNT] = {0};

  if (tach_id >= FAN_TACH_CNT || !is_fan_present(tach_id/2))
    return -1;

  ret = sensors_read(fan_chips[tach_id%8/2], sensor_map[fru].map[sensor_num].snr_name, value);
  if (*value == 0) {
    retry[tach_id]++;
    return retry_err_handle(retry[tach_id], 2);
  }

  retry[tach_id] = 0;
  return ret;
}

//MAX31790 Controller
int
pal_get_fan_name(uint8_t num, char *name) {
  if (num >= pal_tach_cnt) {
    syslog(LOG_WARNING, "%s: invalid fan#:%d", __func__, num);
    return -1;
  }

  sprintf(name, "Fan %d %s", num/2, num%2==0? "In":"Out");
  return 0;
}

int
pal_set_fan_speed(uint8_t fan, uint8_t pwm) {

  int pwm_map[4] = {1, 3, 4, 6};
  char label[32] = {0};

  if (fan >= pal_pwm_cnt || !is_fan_present(fan)) {
    syslog(LOG_INFO, "%s: fan number is invalid - %d", __func__, fan);
    return -1;
  }

  snprintf(label, sizeof(label), "pwm%d", pwm_map[fan/4]);
  return sensors_write(fan_chips[fan%4], label, (float)pwm);
}

int
pal_get_fan_speed(uint8_t tach, int *rpm) {
  int ret=0;
  uint8_t sensor_num = FAN_SNR_START_INDEX + tach;
  float speed = 0;

  if (tach >= pal_tach_cnt || !is_fan_present(tach/2)) {
    syslog(LOG_INFO, "%s: tach number is invalid - %d", __func__, tach);
    return -1;
  }

  ret = read_fan_speed(FRU_MB, sensor_num, &speed);
  *rpm = (int)speed;
  return ret;
}

int
pal_get_pwm_value(uint8_t tach, uint8_t *value) {
  int pwm_map[4] = {1, 3, 4, 6};
  char label[32] = {0};
  uint8_t fan = tach/2;
  float pwm_val;

  if (fan >= pal_pwm_cnt || !is_fan_present(fan)) {
    syslog(LOG_INFO, "%s: fan number is invalid - %d", __func__, fan);
    return -1;
  }

  snprintf(label, sizeof(label), "pwm%d", pwm_map[fan/4]);
  sensors_read(fan_chips[fan%4], label, &pwm_val);
  *value = (uint8_t) pwm_val;

  return 0;
}