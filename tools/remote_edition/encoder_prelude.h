#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define ARM_STM32_HAL
#define _IRSND_H_
#define M1_INFRARED_H_
#define TRUE 1
#define FALSE 0
#define IRSND_ENDLESS_REPETITION 15
#define IRSND_REPETITION_MASK 15
#define IRSND_RAW_REPETITION_FRAME 16
#define IR_ENCODE_CARRIER_PRESCALE_FACTOR 10
#define IR_OTA_PULSE_BIT_MASK 1
#define IR_OTA_SPACE_BIT_MASK 0xfffe
#define TIM_COUNTERMODE_UP 0
#define TIM_CLOCKDIVISION_DIV1 0
#define TIM_AUTORELOAD_PRELOAD_DISABLE 0
#define TIM_TRGO_RESET 0
#define TIM_MASTERSLAVEMODE_DISABLE 0
#define TIM_OCMODE_PWM1 1
#define TIM_OCPOLARITY_HIGH 0
#define TIM_OCNPOLARITY_HIGH 0
#define TIM_OCFAST_DISABLE 0
#define TIM_OCIDLESTATE_RESET 0
#define TIM_OCNIDLESTATE_RESET 0
#define HAL_OK 0
#define TIM_CCER_CC1NE 4u
#define TIM_BDTR_MOE (1u << 15)
#define TIM_CR1_CEN 1u
typedef struct {uint8_t protocol;uint16_t address,command;uint8_t flags;} IRMP_DATA;
typedef struct {uint32_t CCER, BDTR, CR1;} TIM_TypeDef;
typedef struct {uint32_t Prescaler,CounterMode,Period,ClockDivision,AutoReloadPreload;} TimerInit;
typedef struct {TIM_TypeDef *Instance; TimerInit Init;} TIM_HandleTypeDef;
typedef struct {uint32_t MasterOutputTrigger,MasterSlaveMode,MasterOutputTrigger2;} TIM_MasterConfigTypeDef;
typedef struct {uint32_t OCMode,Pulse,OCPolarity,OCNPolarity,OCFastMode,OCIdleState,OCNIdleState;} TIM_OC_InitTypeDef;
static int carrier_failure;
static unsigned HAL_RCC_GetPCLK2Freq(void){return 250000000;}
static void Error_Handler(void){assert(0);}
static int HAL_TIM_PWM_Init(TIM_HandleTypeDef *t){assert(t->Init.Period==9);return HAL_OK;}
static int HAL_TIMEx_MasterConfigSynchronization(TIM_HandleTypeDef *t,TIM_MasterConfigTypeDef *c){(void)t;assert(!c->MasterOutputTrigger&&!c->MasterSlaveMode);return HAL_OK;}
static int HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *t,TIM_OC_InitTypeDef *c,unsigned channel){(void)t;assert(channel==12 && c->Pulse==4);return HAL_OK;}
static int HAL_TIMEx_PWMN_Start(TIM_HandleTypeDef *t,unsigned channel){
    if (carrier_failure==1) return 1;
    t->Instance->CCER = TIM_CCER_CC1NE << channel;
    t->Instance->BDTR = carrier_failure==2 ? 0 : TIM_BDTR_MOE;
    t->Instance->CR1 = TIM_CR1_CEN;
    return HAL_OK;
}
static void HAL_TIMEx_PWMN_Stop(TIM_HandleTypeDef *t,unsigned channel){(void)channel;t->Instance->CCER=t->Instance->BDTR=t->Instance->CR1=0;}
