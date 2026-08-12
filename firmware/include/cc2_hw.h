#ifndef CC2_HW_H
#define CC2_HW_H

#include <stdint.h>

#ifndef __SDCC
#error "Build this firmware with SDCC's MCS-51 target."
#endif

/* XC878 SFR declarations used by the standalone starter. */
__sfr __at(0x80) P0;
__sfr __at(0x86) PORT_SFR_86;
__sfr __at(0x88) TCON;
__sfr __at(0x89) TMOD;
__sfr __at(0x8A) TL0;
__sfr __at(0x8C) TH0;
__sfr __at(0x90) P1;
__sfr __at(0x91) P1_DIR;
__sfr __at(0x92) P5_DATA;
__sfr __at(0x93) P5_DIR;
__sfr __at(0x98) SCON;
__sfr __at(0x99) SBUF;
__sfr __at(0xA8) IE;
__sfr __at(0xA9) SSC_PISEL;
__sfr __at(0xAA) SSC_CONL;
__sfr __at(0xAB) SSC_CONH;
__sfr __at(0xAC) SSC_TBL;
__sfr __at(0xAD) SSC_RBL;
__sfr __at(0xAE) SSC_BRL;
__sfr __at(0xAF) SSC_BRH;
__sfr __at(0xB0) P3;
__sfr __at(0xB1) P3_DIR;
__sfr __at(0xB2) PORT_PAGE;
__sfr __at(0xB3) MODPISEL;
__sfr __at(0xB4) SCU_IRCON0;
__sfr __at(0xB5) SCU_IRCON1;
__sfr __at(0xB6) SCU_SFR_B6;
__sfr __at(0xB7) SCU_SFR_B7;
__sfr __at(0xBA) SCU_SFR_BA;
__sfr __at(0xBB) SCU_SFR_BB;
__sfr __at(0xBD) UART_BCON;
__sfr __at(0xBE) UART_BG;
__sfr __at(0xBE) SCU_SFR_BE;
__sfr __at(0xBF) MOD_PAGE;
__sfr __at(0xC8) P4;
__sfr __at(0xC9) P4_DIR;
__sfr __at(0xE8) IEN1;
__sfr __at(0xEA) SCU_SFR_EA;

__sbit __at(0x98) RI;
__sbit __at(0x99) TI;
__sbit __at(0x8C) TR0;
__sbit __at(0xAF) EA;

__sbit __at(0x83) P0_3;
__sbit __at(0x84) P0_4;
__sbit __at(0x85) P0_5;
__sbit __at(0x86) P0_6;
__sbit __at(0x87) P0_7;

__sbit __at(0x92) P1_2;
__sbit __at(0x93) P1_3;
__sbit __at(0x94) P1_4;
__sbit __at(0x95) P1_5;
__sbit __at(0x97) P1_7;

__sbit __at(0xB0) P3_0;
__sbit __at(0xB1) P3_1;
__sbit __at(0xB4) P3_4;
__sbit __at(0xB5) P3_5;
__sbit __at(0xB6) P3_6;
__sbit __at(0xB7) P3_7;

__sbit __at(0xC8) P4_0;
__sbit __at(0xC9) P4_1;
__sbit __at(0xCA) P4_2;
__sbit __at(0xCC) P4_4;
__sbit __at(0xCD) P4_5;
__sbit __at(0xCE) P4_6;
__sbit __at(0xCF) P4_7;

#define CC2_PORT_PAGE_DATA 0u
#define CC2_TIMER0_INTERRUPT_VECTOR 1u
#define CC2_SSC_INTERRUPT_VECTOR 7u
#define CC2_EXTERNAL_3_6_INTERRUPT_VECTOR 9u
#define CC2_MOD_PAGE_SCU_INTERRUPT 0x80u
#define CC2_MOD_PAGE_SSC_INTERRUPT 0x90u
#define CC2_SCU_IRCON0_EXINT4_MASK 0x10u
#define CC2_SCU_IRCON1_SSC_ERROR_MASK 0x01u
#define CC2_SCU_IRCON1_SSC_RECEIVE_MASK 0x02u
#define CC2_SCU_IRCON1_SSC_TRANSMIT_MASK 0x04u
#define CC2_TIMER0_RELOAD_HIGH 0xD1u
#define CC2_TIMER0_RELOAD_LOW 0x20u
#define CC2_UART_RX_MASK 0x01u
#define CC2_UART_TX_MASK 0x02u

#define CC2_P5_DIRECTION_INPUT_MASK 0x01u
#define CC2_P5_AUX_OUTPUT_MASK 0x04u
#define CC2_P5_GROUNDED_MASK 0x08u
#define CC2_I2C_SCL_MASK 0x20u
#define CC2_I2C_SDA_MASK 0x80u

#define CHARGE_ENABLE_RELAY_N P3_4
#define MOTOR_RELAY_ENABLE P3_5
#define FULL_CHARGE_RELAY P3_6
#define FAN_RELAY P3_7
#define DIRECTION_RELAY P0_3
#define FOOTSWITCH_RELAY P0_4
#define DCDC_FORCE_OUTPUT P1_7
#define BUZZER_OUTPUT P4_2

#define CHARGER_INPUT_DETECT P4_0
#define SPEED_SIGNAL_PULSE P4_1
#define IGNITION_INPUT_DETECT P4_4
#define TRIP_BUTTON P0_5
#define DASH_PIN_16 P0_6
#define DASH_PIN_15 P0_7
#define ADC_ACTIVITY_LED_N P3_0
#define UART_TX_LED_N P3_1

#define VMS_SPI_SCK P1_2
#define VMS_SPI_MOSI P1_3
#define VMS_SPI_MISO P1_4
#define VMS_SPI_CS_N P1_5

#define CC2_AT25128_SIZE 16384u
#define CC2_AT25128_PAGE_SIZE 64u
#define CC2_AT25_WRSR 0x01u
#define CC2_AT25_WRITE 0x02u
#define CC2_AT25_READ 0x03u
#define CC2_AT25_WRDI 0x04u
#define CC2_AT25_RDSR 0x05u
#define CC2_AT25_WREN 0x06u
#define CC2_AT25_STATUS_WIP 0x01u
#define CC2_AT25_STATUS_WEL 0x02u

#define CC2_SSC_CONL_INIT 0x37u
#define CC2_SSC_CONH_INIT 0x46u
#define CC2_SSC_CONH_ENABLE 0x80u
#define CC2_SSC_BRL_INIT 0x11u

#define VOLTAGE_MUX_A2 P4_5
#define VOLTAGE_MUX_A1 P4_6
#define VOLTAGE_MUX_A0 P4_7

#endif
