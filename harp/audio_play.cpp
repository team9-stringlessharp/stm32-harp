/**
  ******************************************************************************
  * @file    BSP/Src/audio.c_play
  * @author  MCD Application Team
  * @brief   This example code shows how to use the audio feature in the
  *          stm32469i_discovery driver
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2017 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdio.h>
#include <faust_dsp.hpp>
#include <faust/sine.hpp>
#include <fast_math.hpp>

/** @addtogroup STM32F4xx_HAL_Examples
  * @{
  */

/** @addtogroup BSP
  * @{
  */

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/
#ifndef DSP_CLASS_NAME
#warning Define DSP_CLASS_NAME!
#define DSP_CLASS_NAME default
#endif

#define MACRO_JOIN_( a, b ) a ## b
#define MACRO_JOIN( a, b ) MACRO_JOIN_( a, b )
#define DSP_CLASS_PREFIX faust_dsp_
#define DSP_CLASS MACRO_JOIN( DSP_CLASS_PREFIX, DSP_CLASS_NAME )
#define DSP_CLASS_HEADER <faust/DSP_CLASS_NAME.hpp>
#include DSP_CLASS_HEADER

/*Since SysTick is set to 1ms (unless to set it quicker) */
/* to run up to 48khz, a buffer around 1000 (or more) is requested*/
/* to run up to 96khz, a buffer around 2000 (or more) is requested*/
#define AUDIO_DEFAULT_VOLUME    70

/* Audio file size and start address are defined here since the audio file is
   stored in Flash memory as a constant table of 16-bit data */
#define AUDIO_START_OFFSET_ADDRESS    0            /* Offset relative to audio file header size */
#define AUDIO_BUFFER_SIZE            2048

/* Private typedef -----------------------------------------------------------*/
typedef enum {
  AUDIO_STATE_IDLE = 0,
  AUDIO_STATE_INIT,
  AUDIO_STATE_PLAYING,
}AUDIO_PLAYBACK_StateTypeDef;

typedef enum {
  BUFFER_OFFSET_NONE = 0,
  BUFFER_OFFSET_HALF,
  BUFFER_OFFSET_FULL,
}BUFFER_StateTypeDef;

typedef struct {
  float faustbuff[AUDIO_BUFFER_SIZE];
  uint32_t buff[AUDIO_BUFFER_SIZE];
  uint32_t fptr;
  BUFFER_StateTypeDef state;
  uint32_t AudioFileSize;
  uint32_t *SrcAddress;
}AUDIO_BufferTypeDef;

typedef enum {
  TS_ACT_NONE = 0,
  TS_ACT_FREQ_DOWN,
  TS_ACT_FREQ_UP,
  TS_ACT_VOLUME_DOWN,
  TS_ACT_VOLUME_UP,
  TS_ACT_PAUSE = 0xFE
}TS_ActionTypeDef;

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
static AUDIO_BufferTypeDef  buffer_ctl;
static AUDIO_PLAYBACK_StateTypeDef  audio_state;
__IO uint32_t uwVolume = 20;
__IO uint32_t uwPauseEnabledStatus = 0;

static uint32_t AudioFreq[9] =
  {
    8000 , 11025, 16000, 22050, 32000, 44100, 48000, 96000, 192000
  };

static TS_ActionTypeDef ts_action = TS_ACT_NONE;

// Faust DSP class instance
static faust_dsp *dsp;

// Buffer management
volatile bool first_half_ready = false;
volatile bool second_half_ready = false;
volatile int audio_underrun_counter = 0;

/* Private function prototypes -----------------------------------------------*/
static void AudioPlay_SetHint(void);
static uint32_t GetData(void *pdata, uint32_t offset, uint8_t *pbuf, uint32_t NbrOfData);

/* Private functions ---------------------------------------------------------*/

//! Clamps float data to specified range
static inline float clamp( float x, float min, float max )
{
	if ( x > max ) return max;
	else if ( x < min ) return min;
	else return x;
}

//! Converts a float sample to uint32 format required by DMA via 24 bit in which the codec expects the data. Clamps float data as well.
static inline uint32_t float_to_dma( float x )
{
	return static_cast<uint32_t>( static_cast<int32_t>( 838860 * clamp( x, -1.f, 1.f ) ) );
}

/**
  * @brief  Audio Play demo
  * @param  None
  * @retval None
  */
void AudioPlay_demo (void)
{
  uint8_t ts_status = TS_OK;
  uint32_t *AudioFreq_ptr;
  AudioFreq_ptr = AudioFreq+0; /*AF_8K = 0, AF_48k = 6*/
  uint8_t FreqStr[256] = {0};
  Point Points2[] = {{100, 140}, {160, 180}, {100, 220}};

  // The DSP
  faust_dsp dsp_instance( new DSP_CLASS, 8000 );
  dsp = &dsp_instance;

  const std::unordered_map<float*, faust_control> &controls = dsp->get_controls( );
  // PRIYANKA Modify for slider input basically
  /*
  for ( const auto &[ptr, ctl] : controls )
  {

  }
  */

  uwPauseEnabledStatus = 1; /* 0 when audio is running, 1 when Pause is on */
  uwVolume = AUDIO_DEFAULT_VOLUME;


  if (TouchScreen_IsCalibrationDone() == 0)
  {
    ts_status = Touchscreen_Calibration();
    if(ts_status == TS_OK)
    {
      BSP_LCD_DisplayStringAt(0, BSP_LCD_GetYSize() - 65, (uint8_t *)"Touchscreen calibration success.", CENTER_MODE);
    }
    else
    {
      BSP_LCD_DisplayStringAt(0, BSP_LCD_GetYSize() - 65, (uint8_t *)"ERROR : touchscreen not yet calibrated.", CENTER_MODE);
      ts_status = TS_ERROR;
    }
  } /* of if (TouchScreen_IsCalibrationDone() == 0) */

  AudioPlay_SetHint();
  BSP_LCD_SetFont(&Font20);


/*  if(BSP_AUDIO_OUT_Init(OUTPUT_DEVICE_SPEAKER, uwVolume, *AudioFreq_ptr) == 0)
  if(BSP_AUDIO_OUT_Init(OUTPUT_DEVICE_BOTH, uwVolume, *AudioFreq_ptr) == 0)*/
  if(BSP_AUDIO_OUT_Init(OUTPUT_DEVICE_BOTH, uwVolume, *AudioFreq_ptr) == 0)
  {
    BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
    BSP_LCD_SetTextColor(LCD_COLOR_GREEN);
    BSP_LCD_DisplayStringAt(0, LINE(6), (uint8_t *)"  AUDIO CODEC   OK  ", CENTER_MODE);
  }
  else
  {
    BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
    BSP_LCD_SetTextColor(LCD_COLOR_RED);
    BSP_LCD_DisplayStringAt(0, LINE(6), (uint8_t *)"  AUDIO CODEC  FAIL ", CENTER_MODE);
    BSP_LCD_DisplayStringAt(0, LINE(7), (uint8_t *)" Try to reset board ", CENTER_MODE);
  }

  /*
  Start playing the file from a circular buffer, once the DMA is enabled, it is
  always in running state. Application has to fill the buffer with the audio data
  using Transfer complete and/or half transfer complete interrupts callbacks
  (DISCOVERY_AUDIO_TransferComplete_CallBack() or DISCOVERY_AUDIO_HalfTransfer_CallBack()...
  */
  AUDIO_Play_Start((uint32_t *)AUDIO_SRC_FILE_ADDRESS, (uint32_t)AUDIO_FILE_SIZE);

  /* Display the state on the screen */
  BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
  BSP_LCD_SetTextColor(LCD_COLOR_BLUE);
  BSP_LCD_DisplayStringAt(0, LINE(8), (uint8_t *)"       PLAYING...     ", CENTER_MODE);

  sprintf((char*)FreqStr, "       VOL:    %3lu     ", uwVolume);
  BSP_LCD_DisplayStringAt(0,  LINE(9), (uint8_t *)FreqStr, CENTER_MODE);

  sprintf((char*)FreqStr, "      FREQ: %6lu     ", *AudioFreq_ptr);
  BSP_LCD_DisplayStringAt(0, LINE(10), (uint8_t *)FreqStr, CENTER_MODE);

  BSP_LCD_SetFont(&Font16);
  BSP_LCD_DisplayStringAt(0, BSP_LCD_GetYSize() - 40, (uint8_t *)"Hear nothing ? Have you copied the audio file with STM-LINK UTILITY ?", CENTER_MODE);

  if(ts_status == TS_OK)
  {
    /* Set touchscreen in Interrupt mode and program EXTI accordingly on falling edge of TS_INT pin */
    ts_status = BSP_TS_ITConfig();
    BSP_TEST_APPLI_ASSERT(ts_status != TS_OK);
    Touchscreen_DrawBackground_Circle_Buttons(16);
  }

  BSP_LCD_SetFont(&Font20);

  /* draw play triangle */
  BSP_LCD_SetTextColor(LCD_COLOR_GREEN);
  BSP_LCD_FillPolygon(Points2, 3);

/* IMPORTANT:
     AUDIO_Play_Process() is called by the SysTick Handler, as it should be called
     within a periodic process */

  /* Infinite loop */
  while (1)
  {
    BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
    BSP_LCD_SetTextColor(LCD_COLOR_BLUE);


    // AUDIO_Play_Process();


    /* Get the TouchScreen State */
    ts_action = (TS_ActionTypeDef) TouchScreen_GetTouchPosition();

    switch (ts_action)
    {
      case TS_ACT_VOLUME_UP:
        /* Increase volume by 5% */
        if (uwVolume < 95)
          uwVolume += 5;
        else
          uwVolume = 100;
        sprintf((char*)FreqStr, "       VOL:    %3lu     ", uwVolume);
        BSP_AUDIO_OUT_SetVolume(uwVolume);
        BSP_LCD_DisplayStringAt(0, LINE(9), (uint8_t *)FreqStr, CENTER_MODE);
        break;
      case TS_ACT_VOLUME_DOWN:
        /* Decrease volume by 5% */
        if (uwVolume > 5)
          uwVolume -= 5;
        else
          uwVolume = 0;
        sprintf((char*)FreqStr, "       VOL:    %3lu     ", uwVolume);
        BSP_AUDIO_OUT_SetVolume(uwVolume);
        BSP_LCD_DisplayStringAt(0, LINE(9), (uint8_t *)FreqStr, CENTER_MODE);
        break;
      case TS_ACT_FREQ_DOWN:
        /*Decrease Frequency */
    	  // change this to if curr freq > max slider freq
        if (*AudioFreq_ptr != 8000)
        {
          sprintf((char*)FreqStr, "      FREQ: %6lu     ", *AudioFreq_ptr);
          BSP_AUDIO_OUT_Pause();
          // PRIYANKA
          // change the hslider frequency of sine.hpp by the step amount
          // do nothing? Run dsp_compute and fill?
          BSP_AUDIO_OUT_Resume();
          BSP_AUDIO_OUT_SetVolume(uwVolume);
        }
        BSP_LCD_DisplayStringAt(0, LINE(10), (uint8_t *)FreqStr, CENTER_MODE);
        break;
      case TS_ACT_FREQ_UP:
        /* Increase Frequency */
    	  // change this to if curr freq > max slider freq
        if (*AudioFreq_ptr != 96000)
        {
          sprintf((char*)FreqStr, "      FREQ: %6lu     ", *AudioFreq_ptr);
          BSP_AUDIO_OUT_Pause();
          // PRIYANKA
          // change the hslider frequency of sine.hpp by the step amount
          // do nothing? Run dsp_compute and fill?
          BSP_AUDIO_OUT_Resume();
          BSP_AUDIO_OUT_SetVolume(uwVolume);
        }

        BSP_LCD_DisplayStringAt(0, LINE(10), (uint8_t *)FreqStr, CENTER_MODE);
        break;
      case TS_ACT_PAUSE:
        /* Set Pause / Resume */
        if (uwPauseEnabledStatus == 1)
        { /* Pause is enabled, call Resume */
          BSP_AUDIO_OUT_Resume();
          uwPauseEnabledStatus = 0;
          BSP_LCD_DisplayStringAt(0, LINE(8), (uint8_t *)"       PLAYING...     ", CENTER_MODE);
          BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
          BSP_LCD_FillPolygon(Points2, 3);
          BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
          BSP_LCD_FillRect(100, 140, 25 , 80);
          BSP_LCD_FillRect(140, 140, 25 , 80);
        }
        else
        { /* Pause the playback */
          BSP_AUDIO_OUT_Pause();
          uwPauseEnabledStatus = 1;
          BSP_LCD_DisplayStringAt(0, LINE(8), (uint8_t *)"       PAUSE  ...     ", CENTER_MODE);
          BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
          BSP_LCD_FillRect(100, 140, 80 , 80);
          BSP_LCD_SetTextColor(LCD_COLOR_GREEN);
          BSP_LCD_FillPolygon(Points2, 3);
        }
        HAL_Delay(200);
        break;

      default:
        break;
    }



    /* Toggle LED4 */
    BSP_LED_Toggle(LED4);

    /* Insert 100 ms delay */
    // HAL_Delay(100);
    if (CheckForUserInput() > 0)
    {
      /* Set LED4 */
      BSP_LED_On(LED4);

      //BSP_AUDIO_OUT_Stop(CODEC_PDWN_SW);
      return;
    }
  }
}

/**
  * @brief  Display Audio demo hint
  * @param  None
  * @retval None
  */
static void AudioPlay_SetHint(void)
{
  /* Clear the LCD */
  BSP_LCD_Clear(LCD_COLOR_WHITE);

  /* Set Audio Demo description */
  BSP_LCD_SetTextColor(LCD_COLOR_BLUE);
  BSP_LCD_FillRect(0, 0, BSP_LCD_GetXSize(), 90);
  BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
  BSP_LCD_SetBackColor(LCD_COLOR_BLUE);
  BSP_LCD_SetFont(&Font24);
  BSP_LCD_DisplayStringAt(0, 0, (uint8_t *)"AUDIO EXAMPLE", CENTER_MODE);
  BSP_LCD_SetFont(&Font12);
  BSP_LCD_DisplayStringAt(0, 30, (uint8_t *)"Press User button for next menu          ", CENTER_MODE);
  BSP_LCD_DisplayStringAt(0, 45, (uint8_t *)"Use touch screen +/- to change Volume    ", CENTER_MODE);
  BSP_LCD_DisplayStringAt(0, 60, (uint8_t *)"Use touch screen +/- to change Frequency ", CENTER_MODE);
  BSP_LCD_DisplayStringAt(0, 75, (uint8_t *)"Touch upper part of the screen to Pause/Resume    ", CENTER_MODE);

  /* Set the LCD Text Color */
  BSP_LCD_SetTextColor(LCD_COLOR_BLUE);
  BSP_LCD_DrawRect(10, 100, BSP_LCD_GetXSize() - 20, BSP_LCD_GetYSize() - 110);
  BSP_LCD_DrawRect(11, 101, BSP_LCD_GetXSize() - 22, BSP_LCD_GetYSize() - 112);

}


/**
  * @brief  Starts Audio streaming.
  * @param  None
  * @retval Audio error
  */
AUDIO_ErrorTypeDef AUDIO_Play_Start(uint32_t *psrc_address, uint32_t file_size)
{
  // uint32_t bytesread;

  buffer_ctl.state = BUFFER_OFFSET_NONE;
  /*
  buffer_ctl.AudioFileSize = file_size;
  buffer_ctl.SrcAddress = psrc_address;

  bytesread = GetData( (void *)psrc_address,
                       0,
                       &buffer_ctl.buff[0],
                       AUDIO_BUFFER_SIZE);
  if(bytesread > 0)
  {
    BSP_AUDIO_OUT_Play((uint16_t *)&buffer_ctl.buff[0], AUDIO_BUFFER_SIZE);
    audio_state = AUDIO_STATE_PLAYING;
    buffer_ctl.fptr = bytesread;
    return AUDIO_ERROR_NONE;
  }
  return AUDIO_ERROR_IO;
  */

  float* ptr = &(buffer_ctl.faustbuff[0]);
  dsp->compute( AUDIO_BUFFER_SIZE, (float**)NULL , &(ptr) );
  for (int i = 0; i < AUDIO_BUFFER_SIZE; i++)
  {
	  buffer_ctl.buff[i] = float_to_dma(buffer_ctl.faustbuff[i]);
  }
  first_half_ready = false;
  second_half_ready = false;
  buffer_ctl.state = BUFFER_OFFSET_NONE;
  BSP_AUDIO_OUT_Play((uint16_t*)&buffer_ctl.buff[0], AUDIO_BUFFER_SIZE * 4);
  audio_state = AUDIO_STATE_PLAYING;
  return AUDIO_ERROR_NONE;
}

/**
  * @brief  Manages Audio process.
  * @param  None
  * @retval Audio error
  */
uint8_t AUDIO_Play_Process(void)
{
  // uint32_t bytesread;
  AUDIO_ErrorTypeDef error_state = AUDIO_ERROR_NONE;

  switch(audio_state)
  {
  case AUDIO_STATE_PLAYING:

    /* 1st half buffer played; so fill it and continue playing from bottom*/
    if(buffer_ctl.state == BUFFER_OFFSET_HALF)
    {
	/*
      bytesread = GetData((void *)buffer_ctl.SrcAddress,
                          buffer_ctl.fptr,
                          &buffer_ctl.buff[0],
                          AUDIO_BUFFER_SIZE /2);
	 */
      while (!first_half_ready);
      float* ptr = &(buffer_ctl.faustbuff[0]);
      dsp->compute( AUDIO_BUFFER_SIZE / 2, (float**)NULL , &(ptr) );
      for (int i = 0; i < AUDIO_BUFFER_SIZE / 2; i++)
      {
    	  buffer_ctl.buff[i] = float_to_dma(buffer_ctl.faustbuff[i]);
      }
      first_half_ready = false;
      buffer_ctl.state = BUFFER_OFFSET_NONE;
    }

    /* 2nd half buffer played; so fill it and continue playing from top */
    if(buffer_ctl.state == BUFFER_OFFSET_FULL)
    {
    	/*
      bytesread = GetData((void *)buffer_ctl.SrcAddress,
                          buffer_ctl.fptr,
                          &buffer_ctl.buff[AUDIO_BUFFER_SIZE /2],
                          AUDIO_BUFFER_SIZE /2);
        */
        while (!second_half_ready);
        float* ptr = &(buffer_ctl.faustbuff[AUDIO_BUFFER_SIZE/2]);
        dsp->compute( AUDIO_BUFFER_SIZE / 2, (float**)NULL, &(ptr));
        for (int i = AUDIO_BUFFER_SIZE / 2; i < AUDIO_BUFFER_SIZE; i++)
        {
      	  buffer_ctl.buff[i] = float_to_dma(buffer_ctl.faustbuff[i]);
        }
        second_half_ready = false;
        buffer_ctl.state = BUFFER_OFFSET_NONE;
    }
    break;

  default:
    error_state = AUDIO_ERROR_NOTREADY;
    break;
  }
  return (uint8_t) error_state;
}

/**
  * @brief  Gets Data from storage unit.
  * @param  None
  * @retval None
  */
static uint32_t GetData(void *pdata, uint32_t offset, uint8_t *pbuf, uint32_t NbrOfData)
{
  uint8_t *lptr = (uint8_t*)pdata;
  uint32_t ReadDataNbr;

  ReadDataNbr = 0;
  while(((offset + ReadDataNbr) < buffer_ctl.AudioFileSize) && (ReadDataNbr < NbrOfData))
  {
    pbuf[ReadDataNbr]= lptr [offset + ReadDataNbr];
    ReadDataNbr++;
  }
  return ReadDataNbr;
}

/*------------------------------------------------------------------------------
       Callbacks implementation:
           the callbacks API are defined __weak in the stm32469i_discovery_audio.c file
           and their implementation should be done the user code if they are needed.
           Below some examples of callback implementations.
  ----------------------------------------------------------------------------*/
/**
  * @brief  Manages the full Transfer complete event.
  * @param  None
  * @retval None
  */
void BSP_AUDIO_OUT_TransferComplete_CallBack(void)
{
  if(audio_state == AUDIO_STATE_PLAYING)
  {
    /* allows AUDIO_Play_Process() to refill 2nd part of the buffer  */
    buffer_ctl.state = BUFFER_OFFSET_FULL;
    first_half_ready = false;
    second_half_ready = true;
  }
}

/**
  * @brief  Manages the DMA Half Transfer complete event.
  * @param  None
  * @retval None
  */
void BSP_AUDIO_OUT_HalfTransfer_CallBack(void)
{
  if(audio_state == AUDIO_STATE_PLAYING)
  {
    /* allows AUDIO_Play_Process() to refill 1st part of the buffer  */
    buffer_ctl.state = BUFFER_OFFSET_HALF;
    first_half_ready = true;
    second_half_ready = false;
  }
}

/**
  * @brief  Manages the DMA FIFO error event.
  * @param  None
  * @retval None
  */
void BSP_AUDIO_OUT_Error_CallBack(void)
{
  /* Display message on the LCD screen */
  BSP_LCD_SetBackColor(LCD_COLOR_RED);
  BSP_LCD_DisplayStringAt(0, LINE(14), (uint8_t *)"       DMA  ERROR     ", CENTER_MODE);
  BSP_LCD_SetBackColor(LCD_COLOR_WHITE);

  /* Stop the program with an infinite loop */
  while (BSP_PB_GetState(BUTTON_USER) != PB_RESET)
  {
    return;
  }

  /* could also generate a system reset to recover from the error */
  /* .... */
}

/**
  * @}
  */

/**
  * @}
  */
