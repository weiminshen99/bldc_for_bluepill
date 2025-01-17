//
//	Copyright (C) AAARI Corporation, 2022-2023, <weiminshen99@gmail.com>
//
//	Sensor Values are read by ADC1, transered by DMA1_Channel1 to memory adc_buffer
//	ADC1 is init to be triggered by TIM3 with period = 2000 (or 0.125 ms)
//	The converted data is transfer to adc_buffer by DMA1_Channel1 upon completation
//	The DMA interrupt is handled by DMA1_Channel1_IRQHandler()
//

#include "defines.h"
#include "buzzer.h"
#include "bldc.h"
#include "sense.h"

extern volatile State_t State;

ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;
TIM_HandleTypeDef htim3;
volatile adc_buf_t adc_buffer;

const uint8_t map_h_val_to_h_pos[8] = {0,0,2,1,4,5,3,6};

//extern int H_Sector;

// ==================================================================================
void Sensors_Trigger_Start(uint8_t trigger)
{
    ADC1_Init();
    DMA1_Init();
    HALL_Init();

    if (trigger==1) {
  	ADC1->CR2 &= ~ADC_CR2_EXTSEL; // set ExternalTriggerSource = 000 (i.e., T1_CC1)
	Motor_Timer_Start();	// default: start TIM1 to trigger ADC1
	// WARNING: ADC1 will stop if TIM1_PWM1 becomes inactivity sometimes, Caution
	//TIM1->CCR1 = 1000;	// make sure initially TIM1 PWM1 is active to trigger ADC1
    } else if (trigger==2) {
  	ADC1->CR2 &= ~ADC_CR2_EXTSEL_2; // external trigger source = 011 (i.e., T2_CC2)
  	ADC1->CR2 |= ADC_CR2_EXTSEL_1; // external trigger source = 011 (i.e., T2_CC2)
  	ADC1->CR2 |= ADC_CR2_EXTSEL_0; // external trigger source = 011 (i.e., T2_CC2)
        Buzzer_Start();		// start TIM2 to trigger ADC1
    } else { // defalt is TIM3
  	//ADC1->CR2 != ADC_CR2_EXTSEL_2; // external trigger source = 100 (i.e., T3_TRGO)
  	//ADC1->CR2 &= ~ADC_CR2_EXTSEL_1; // external trigger source = 100 (i.e., T3_TRGO)
  	//ADC1->CR2 &= ~ADC_CR2_EXTSEL_0; // external trigger source = 100 (i.e., T3_TRGO)
        TIM3_Init();
        HAL_TIM_Base_Start(&htim3); // TIM3 (T3_TRGO) will trigger ADC1
    }

    // now that trigger is runing, we can start ADC1 and it will run automatically
    HAL_ADC_Start(&hadc1);
    HAL_ADCEx_Calibration_Start(&hadc1);
}

// =============================================================
// Sample frequence = ~ 18KHz or 28.0us
//
void DMA1_Channel1_IRQHandler(void)
{
    DMA1->IFCR = DMA_IFCR_CTCIF1; // clear flag

    //HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13); // show the sample frequency

    Emergency_Shut_Down();

    Process_Raw_Sensor_Data();
}

// ==============================================================
void Emergency_Shut_Down(void)
{

}

// Assume Ia+Ib+Ic=0; you can compute q if you know the other two
//
inline int blockPhaseCurrent(int pos, int u, int v)
{
  switch(pos) {
    case 0: return(u-v);// u=0, v=pwm, w=-pwm
    case 1: return(u); 	// *u = -pwm; // *v = pwm; // *w = 0;
    case 2: return(u); 	// *u = -pwm; // *v = 0; // *w = pwm;
    case 3: return(v); 	// *u = 0;   // *v = -pwm; // *w = pwm;
    case 4: return(v); 	// *u = pwm; // *v = -pwm; // *w = 0;
    case 5: return(-(u-v));  // *u = pwm;  // *v = 0;  // *w = -pwm;
    default: return(0); // *u = 0;   // *v = 0;   // *w = 0;
   }
}

// ==========================================================================
void Process_Raw_Sensor_Data()
{
    if (State.SensorCalibCounter > 0) {  // calibrate ADC offsets
      State.SensorCalibCounter--;
      State.Ia = ((adc_buffer.Va<<4) + State.Ia) / 2;
      //State.Ib = ((adc_buffer.Vb<<4) + State.Ib) / 2;
      State.Status = DONE; // continue calabooration
      return;
    }

    // Fill in the information for State

    State.Ia = (adc_buffer.Va<<4); // (State.Ia + (adc_buffer<<4)) / 2;
    State.H_POS_last = State.H_POS_now;
    State.H_POS_now = map_h_val_to_h_pos[HALL_Sense()];

    if (State.H_POS_now == State.H_POS_last) { // no change
	State.Status = READY;	// continue action
	return;
    }

    //
    // Now Update H_Sector, a tricky business
    //
    if (State.PWM_now>=0 && State.H_POS_last==5 && State.H_POS_now<5) { // forward 5->[0..4]
	State.H_Sector_Counter = State.H_Sector_Counter + 1;
	HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
    }
    if (State.PWM_now<=0 && State.H_POS_last==0 && State.H_POS_now>0) { // backward 0->[5..1]
	State.H_Sector_Counter = State.H_Sector_Counter - 1;
	HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
    }

    State.ANGLE_now = State.H_Sector_Counter * MOTOR_H_SECTOR_SIZE +
		((State.PWM_now>=0) ? 1 : -1) * State.H_POS_now * MOTOR_H_STEP_SIZE;

    State.Status = READY;

    //disable PWM when current limit is reached (current chopping)
    //if (ABS((adc_buffer[2] - offset_Iout) * MOTOR_AMP_CONV_DC_AMP)  > DC_CUR_LIMIT ) {
    //if (timeout > TIMEOUT || motor_enable == 0) {
    //   MOTOR_TIM->BDTR &= ~TIM_BDTR_MOE;
    // } else {
    //   MOTOR_TIM->BDTR |= TIM_BDTR_MOE;
    // }

    //setScopeChannel(2, (adc_buffer.rl1 - offsetrl1) / 8);
    //setScopeChannel(3, (adc_buffer.rl2 - offsetrl2) / 8);

    //uint32_t buzzerTimer = 0;

    //float batteryVoltage = BAT_NUMBER_OF_CELLS * 4.0;

    //if (buzzerTimer % 1000 == 0) {  // because you get float rounding errors if it would run every time
    //  batteryVoltage = batteryVoltage * 0.99 + ((float)adc_buffer[3] * ((float)BAT_CALIB_REAL_VOLTAGE / (float)BAT_CALIB_ADC)) * 0.01;
    //}

}


// ==========================================================================
void HALL_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.Mode  = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitStruct.Pin = HALL_U_PIN | HALL_V_PIN | HALL_W_PIN;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

// ==========================================================================
int HALL_Sense(void)
{
  //determine next position based on hall sensors // WHY WHY Negation ??????
  //uint8_t hall_a = !(HALL_U_PORT->IDR & HALL_U_PIN);
  //uint8_t hall_b = !(HALL_V_PORT->IDR & HALL_V_PIN);
  //uint8_t hall_c = !(HALL_W_PORT->IDR & HALL_W_PIN);

  uint8_t hall_a = HAL_GPIO_ReadPin(HALL_U_PORT, HALL_U_PIN);
  uint8_t hall_b = HAL_GPIO_ReadPin(HALL_V_PORT, HALL_V_PIN);
  uint8_t hall_c = HAL_GPIO_ReadPin(HALL_W_PORT, HALL_W_PIN);

  uint8_t hall_pos = hall_a * 1 + hall_b * 2 + hall_c * 4;

  return(hall_pos);
}

// ==========================================================================
void ADC1_Init(void)
{
  __HAL_RCC_ADC1_CLK_ENABLE();

  hadc1.Instance                   = ADC1;
  hadc1.Init.ScanConvMode          = ADC_SCAN_ENABLE;
  hadc1.Init.ContinuousConvMode    = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv      = ADC_EXTERNALTRIGCONV_T3_TRGO; 	// TIM3 trigger
  //hadc1.Init.ExternalTrigConv      = ADC_EXTERNALTRIGCONV_T1_CC1; 	// TIM1->CCR1 trigger
  //hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;		// software trigger
  hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion       = 3;
  HAL_ADC_Init(&hadc1);

  // Configure the ADC multi-mode
  ADC_MultiModeTypeDef multimode;
  multimode.Mode = ADC_DUALMODE_REGSIMULT;
  HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode);

  ADC_ChannelConfTypeDef sConfig;
  sConfig.SamplingTime = ADC_SAMPLETIME_7CYCLES_5;

  sConfig.Channel = ADC_CHANNEL_1;  // PA1
  sConfig.Rank    = ADC_REGULAR_RANK_1;
  HAL_ADC_ConfigChannel(&hadc1, &sConfig);

  sConfig.Channel = ADC_CHANNEL_2;  // PA2
  sConfig.Rank    = ADC_REGULAR_RANK_2;
  HAL_ADC_ConfigChannel(&hadc1, &sConfig);

  //sConfig.SamplingTime = ADC_SAMPLETIME_13CYCLES_5;

  sConfig.Channel = ADC_CHANNEL_3;  // PA3
  sConfig.Rank    = ADC_REGULAR_RANK_3;
  HAL_ADC_ConfigChannel(&hadc1, &sConfig);
/*
  sConfig.Channel = ADC_CHANNEL_4;  // PA4
  sConfig.Rank    = ADC_REGULAR_RANK_4;
  HAL_ADC_ConfigChannel(&hadc1, &sConfig);

  sConfig.Channel = ADC_CHANNEL_5;  // PA5
  sConfig.Rank    = ADC_REGULAR_RANK_5;
  HAL_ADC_ConfigChannel(&hadc1, &sConfig);

  temperature requires at least 17.1uS sampling time
  sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
  sConfig.Channel = ADC_CHANNEL_TEMPSENSOR;  // internal temp
  sConfig.Rank    = 6;
  HAL_ADC_ConfigChannel(&hadc1, &sConfig);
*/
  __HAL_ADC_ENABLE(&hadc1);

  // If and when ADC1 is started with interrupt as HAL_ADC_Start_IT(&hadc1)
  // then please uncomment these two lines to enable interrupt from ADC1
  HAL_NVIC_SetPriority(ADC1_2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(ADC1_2_IRQn);

  // If and when ADC1 wants to use DMA to transfer data to memory, then
  // 1. set ADC1's control register CR2 to use DMA or TSVREFE (page 240)
  // 2. link ADC1 to DMA
  hadc1.Instance->CR2 |= ADC_CR2_DMA | ADC_CR2_TSVREFE;
  hadc1.DMA_Handle = &hdma_adc1;

  // Configure the GPIO Ports for ADC1
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Mode  = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5;
  //GPIO_InitStruct.Pin = GPIO_PIN_7;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

// ===========================================================
void DMA1_Init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();

    hdma_adc1.Instance = DMA1_Channel1;
    hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_adc1.Init.Mode = DMA_CIRCULAR;
    hdma_adc1.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&hdma_adc1) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_DMA_ENABLE(&hdma_adc1);

    HAL_DMA_Start_IT(&hdma_adc1, (uint32_t) &(ADC1->DR), (uint32_t) &(adc_buffer), 3);

    // enable interrupt of DMA1_Channel1
    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
}

// ===========================================================
void ADC1_2_IRQHandler(void)
{   // This callback works if and when ADC1 is started by HAL_ADC_Start_IT(_)
    // this interrupt indicates that ADC1 has completed conversion
    HAL_ADC_IRQHandler(&hadc1);
    __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_EOC); // clear this interrupt flag

    adc_buffer.Va = HAL_ADC_GetValue(&hadc1);	// get data from ADC1 to AD_RES
    //HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);     // show led here
}


// =========================================================
// This is to be used to trigger ADC1 automatically
//
void TIM3_Init(void)
{
  __HAL_RCC_TIM3_CLK_ENABLE();

  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 2000; // ~= 18KHz or 28.0us 
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK) Error_Handler();

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK) Error_Handler();

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK) Error_Handler();
}
