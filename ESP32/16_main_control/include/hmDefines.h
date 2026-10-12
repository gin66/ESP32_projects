//-----------------------------------------------------------------------------
// 2023 Ahoy, https://github.com/lumpapu/ahoy
// Creative Commons - http://creativecommons.org/licenses/by-nc-sa/3.0/de/
//-----------------------------------------------------------------------------

#ifndef __HM_DEFINES_H__
#define __HM_DEFINES_H__

// #include "../utils/dbg.h"
#include <cstdint>

// units
enum {
  UNIT_V = 0,
  UNIT_A,
  UNIT_W,
  UNIT_WH,
  UNIT_KWH,
  UNIT_HZ,
  UNIT_C,
  UNIT_PCT,
  UNIT_VAR,
  UNIT_NONE
};
const char* const units[] = {"V",  "A",  "W", "Wh",  "kWh",
                             "Hz", "°C", "%", "var", ""};

// field types
enum {
  FLD_UDC = 0,
  FLD_IDC,
  FLD_PDC,
  FLD_YD,
  FLD_YW,
  FLD_YT,
  FLD_UAC,
  FLD_UAC_1N,
  FLD_UAC_2N,
  FLD_UAC_3N,
  FLD_UAC_12,
  FLD_UAC_23,
  FLD_UAC_31,
  FLD_IAC,
  FLD_IAC_1,
  FLD_IAC_2,
  FLD_IAC_3,
  FLD_PAC,
  FLD_F,
  FLD_T,
  FLD_PF,
  FLD_EFF,
  FLD_IRR,
  FLD_Q,
  FLD_EVT,
  FLD_FW_VERSION,
  FLD_FW_BUILD_YEAR,
  FLD_FW_BUILD_MONTH_DAY,
  FLD_FW_BUILD_HOUR_MINUTE,
  FLD_HW_ID,
  FLD_ACT_ACTIVE_PWR_LIMIT,
  /*FLD_ACT_REACTIVE_PWR_LIMIT, FLD_ACT_PF,*/ FLD_LAST_ALARM_CODE,
  FLD_MP
};

const char* const fields[] = {
    "U_DC",
    "I_DC",
    "P_DC",
    "YieldDay",
    "YieldWeek",
    "YieldTotal",
    "U_AC",
    "U_AC_1N",
    "U_AC_2N",
    "U_AC_3N",
    "UAC_12",
    "UAC_23",
    "UAC_31",
    "I_AC",
    "IAC_1",
    "I_AC_2",
    "I_AC_3",
    "P_AC",
    "F_AC",
    "Temp",
    "PF_AC",
    "Efficiency",
    "Irradiation",
    "Q_AC",
    "ALARM_MES_ID",
    "FWVersion",
    "FWBuildYear",
    "FWBuildMonthDay",
    "FWBuildHourMinute",
    "HWPartId",
    "active_PowerLimit",
    /*"reactivePowerLimit","Powerfactor",*/ "LastAlarmCode",
    "MaxPower"};
const char* const notAvail = "n/a";

const uint8_t fieldUnits[] = {
    UNIT_V,    UNIT_A,    UNIT_W,   UNIT_WH,   UNIT_KWH,  UNIT_KWH,  UNIT_V,
    UNIT_V,    UNIT_V,    UNIT_V,   UNIT_V,    UNIT_V,    UNIT_V,    UNIT_A,
    UNIT_A,    UNIT_A,    UNIT_A,   UNIT_W,    UNIT_HZ,   UNIT_C,    UNIT_NONE,
    UNIT_PCT,  UNIT_PCT,  UNIT_VAR, UNIT_NONE, UNIT_NONE, UNIT_NONE, UNIT_NONE,
    UNIT_NONE, UNIT_NONE, UNIT_PCT, UNIT_NONE, UNIT_W};

// indices to calculation functions, defined in hmInverter.h
enum {
  CALC_YT_CH0 = 0,
  CALC_YD_CH0,
  CALC_UDC_CH,
  CALC_PDC_CH0,
  CALC_EFF_CH0,
  CALC_IRR_CH,
  CALC_MPAC_CH0,
  CALC_MPDC_CH
};
enum { CMD_CALC = 0xffff };

// CH0 is default channel (freq, ac, temp)
enum { CH0 = 0, CH1, CH2, CH3, CH4, CH5, CH6 };

typedef struct {
  uint8_t fieldId;  // field id
  uint8_t unitId;   // uint id
  uint8_t ch;       // channel 0 - 4
  uint8_t start;    // pos of first byte in buffer
  uint8_t num;      // number of bytes in buffer
  uint16_t div;     // divisor / calc command
} byteAssign_t;

/**
 *  indices are built for the buffer starting with cmd-id in first byte
 *  (complete payload in buffer)
 * */

//-------------------------------------
// HM-Series
//-------------------------------------
const byteAssign_t InfoAssignment[] = {
    {FLD_FW_VERSION, UNIT_NONE, CH0, 0, 2, 1},
    {FLD_FW_BUILD_YEAR, UNIT_NONE, CH0, 2, 2, 1},
    {FLD_FW_BUILD_MONTH_DAY, UNIT_NONE, CH0, 4, 2, 1},
    {FLD_FW_BUILD_HOUR_MINUTE, UNIT_NONE, CH0, 6, 2, 1},
    {FLD_HW_ID, UNIT_NONE, CH0, 8, 2, 1}};
#define HMINFO_LIST_LEN (sizeof(InfoAssignment) / sizeof(byteAssign_t))
#define HMINFO_PAYLOAD_LEN 14

const byteAssign_t SystemConfigParaAssignment[] = {
    {FLD_ACT_ACTIVE_PWR_LIMIT, UNIT_PCT, CH0, 2, 2, 10} /*,
{ FLD_ACT_REACTIVE_PWR_LIMIT,  UNIT_PCT,   CH0,  4, 2, 10   },
{ FLD_ACT_PF,                  UNIT_NONE,  CH0,  6, 2, 1000 }*/
};
#define HMSYSTEM_LIST_LEN \
  (sizeof(SystemConfigParaAssignment) / sizeof(byteAssign_t))
#define HMSYSTEM_PAYLOAD_LEN 14

const byteAssign_t AlarmDataAssignment[] = {
    {FLD_LAST_ALARM_CODE, UNIT_NONE, CH0, 0, 2, 1}};
#define HMALARMDATA_LIST_LEN \
  (sizeof(AlarmDataAssignment) / sizeof(byteAssign_t))
#define HMALARMDATA_PAYLOAD_LEN 0  // 0: means check is off
#define ALARM_LOG_ENTRY_SIZE 12

//-------------------------------------
// HM600, HM700, HM800
//-------------------------------------
const byteAssign_t hm2chAssignment[] = {
    {FLD_UDC, UNIT_V, CH1, 2, 2, 10},
    {FLD_IDC, UNIT_A, CH1, 4, 2, 100},
    {FLD_PDC, UNIT_W, CH1, 6, 2, 10},
    {FLD_YD, UNIT_WH, CH1, 22, 2, 1},
    {FLD_YT, UNIT_KWH, CH1, 14, 4, 1000},
    {FLD_IRR, UNIT_PCT, CH1, CALC_IRR_CH, CH1, CMD_CALC},
    {FLD_MP, UNIT_W, CH1, CALC_MPDC_CH, CH1, CMD_CALC},

    {FLD_UDC, UNIT_V, CH2, 8, 2, 10},
    {FLD_IDC, UNIT_A, CH2, 10, 2, 100},
    {FLD_PDC, UNIT_W, CH2, 12, 2, 10},
    {FLD_YD, UNIT_WH, CH2, 24, 2, 1},
    {FLD_YT, UNIT_KWH, CH2, 18, 4, 1000},
    {FLD_IRR, UNIT_PCT, CH2, CALC_IRR_CH, CH2, CMD_CALC},
    {FLD_MP, UNIT_W, CH2, CALC_MPDC_CH, CH2, CMD_CALC},

    {FLD_UAC, UNIT_V, CH0, 26, 2, 10},
    {FLD_IAC, UNIT_A, CH0, 34, 2, 100},
    {FLD_PAC, UNIT_W, CH0, 30, 2, 10},
    {FLD_Q, UNIT_VAR, CH0, 32, 2, 10},
    {FLD_F, UNIT_HZ, CH0, 28, 2, 100},
    {FLD_PF, UNIT_NONE, CH0, 36, 2, 1000},
    {FLD_T, UNIT_C, CH0, 38, 2, 10},
    {FLD_EVT, UNIT_NONE, CH0, 40, 2, 1},
    {FLD_YD, UNIT_WH, CH0, CALC_YD_CH0, 0, CMD_CALC},
    {FLD_YT, UNIT_KWH, CH0, CALC_YT_CH0, 0, CMD_CALC},
    {FLD_PDC, UNIT_W, CH0, CALC_PDC_CH0, 0, CMD_CALC},
    {FLD_EFF, UNIT_PCT, CH0, CALC_EFF_CH0, 0, CMD_CALC},
    {FLD_MP, UNIT_W, CH0, CALC_MPAC_CH0, 0, CMD_CALC}

};
#define HM2CH_LIST_LEN (sizeof(hm2chAssignment) / sizeof(byteAssign_t))
#define HM2CH_PAYLOAD_LEN 42

#endif /*__HM_DEFINES_H__*/
