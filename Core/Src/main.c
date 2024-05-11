/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usbd_cdc_if.h"
#include "button.h"
#include "config.h"
#include "ESP01.h"
#include <stdbool.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum{
    START,
    HEADER_1,
    HEADER_2,
    HEADER_3,
    NBYTES,
    TOKEN,
    PAYLOAD
}_eProtocolo;

typedef enum{
    ACKNOWLEDGE = 0x0D,
	STARTCONFIG = 0xEE,
    ALIVE = 0xF0,
    FIRMWARE = 0xF1,
	ANALOG_IR = 0xF2,
    UNKNOWNCOMMAND = 0xFF
}_eID;

typedef struct{
    uint8_t newData;
    uint8_t indexStart;
    uint8_t checksumRx;
    uint8_t indexWriteRx;
    uint8_t indexReadRx;
    uint8_t indexWriteTx;
    uint8_t indexReadTx;
    uint8_t bufferRx[256];
    uint8_t bufferTx[256];
}_sDato;

typedef struct{
	uint16_t bufADC[64][8];
	uint16_t dynSumDF[8];
	//uint32_t sumADC[8];
	//uint16_t newData[8];
	uint8_t indexWriteADC;
	uint8_t indexReadADC;
}_sADCtype;

typedef struct{
	uint8_t distanceValues[20];
	uint8_t distanceInMm;
	uint16_t distanceMeasured;
}_sTCRT5000;

typedef union{
    uint8_t  u8[2];
    uint16_t u16;
} _uConvert;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define modeIDLE			0
#define modeONE				1
#define modeTWO				2
#define maxMODES			3

#define viaUART				0
#define viaWIFI				1
#define viaUSB				2

#define PRESSED				0
#define NBYTES				4
#define NUMCHANNELSADC		8
#define SIZEBUFADC			64

#define SIZEBUFESP01		128
#define WIFI_SSID			"WiFi Velasco"
#define WIFI_PASSWORD		"ncgrmvelasco"
#define WIFI_UDP_REMOTEIP	"192.168.1.18"
#define WIFI_UDP_REMOTEPORT	30000
#define WIFI_UDP_LOCALPORT	30000
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
ADC_HandleTypeDef hadc2;
DMA_HandleTypeDef hdma_adc1;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim4;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
_sButton myButton;
_sDato datosComSerie, datosComUSB, datosComWIFI;
_sADCtype datosADC;
_sTCRT5000 datosTCRT5000[8];
_sESP01Handle esp01;
_uConvert myADCbuf[NUMCHANNELSADC];
_eProtocolo estadoProtocolo;

uint8_t mode		= 0;
uint8_t time10ms 	= 40;
uint8_t	time40ms	= 4;
uint8_t time100ms	= 10;
uint8_t time500ms	= 50;
uint32_t maskIDLE	= 0xF0AA8080;
uint32_t maskONE 	= 0xF0800000;
uint32_t maskTWO 	= 0xF0A00000;
uint32_t mask		= 0x80000000;
uint32_t myHB		= 0xF0008000;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_TIM1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_ADC1_Init(void);
static void MX_ADC2_Init(void);
static void MX_TIM4_Init(void);
/* USER CODE BEGIN PFP */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim);

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);

void USB_Receive(uint8_t *buf, uint16_t len);

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc);

void heartbeatTask();

void do10ms();

void do40ms();

void do100ms();

void do500ms();

void decodeProtocol(_sDato *datosCom);

void decodeData(_sDato *);

void communicationTask(_sDato *datosCom, uint8_t source);

void handleUSBCommunication();

void buttonTask(_sButton *button);

void stateTask();

void init_myADC();

void init_myTCRT5000();

void ESP01DoCHPD(uint8_t value);

void ESP01ChangeState(_eESP01STATUS esp01State);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim){
	if (htim->Instance == TIM1)
	{
		if (time10ms)
			time10ms--;
		HAL_ADC_Start_DMA(&hadc1, (uint32_t *)&datosADC.bufADC[datosADC.indexWriteADC], NUMCHANNELSADC);
	}
}


void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc){
	datosADC.indexWriteADC++;
	datosADC.indexWriteADC &= (SIZEBUFADC-1);
}


void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart){
	if (huart->Instance == USART1){
		datosComSerie.indexWriteRx++;
		HAL_UART_Receive_IT(&huart1, &datosComSerie.bufferRx[datosComSerie.indexWriteRx], 1);

		datosComWIFI.indexWriteRx++;
		ESP01_WriteRX(datosComWIFI.bufferRx[datosComWIFI.indexWriteRx]);
	}
}


void USB_Receive(uint8_t *buf, uint16_t len){
	memcpy(&datosComUSB.bufferRx[datosComUSB.indexWriteRx], buf, len);
	datosComUSB.indexWriteRx += len;
	datosComUSB.newData = true;
}


void heartbeatTask(){
	if (myHB & mask)
		HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, 0);	// ON
	else
		HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, 1);	// OFF

	mask >>= 1;									// Displace hbMask one place to the right
	if (!mask)
		mask = 0x80000000;						// If there's a 0 in that place, changes the actual positions to compare the right way

}


void do10ms(){
	// do something
	time10ms = 40;
}


void do40ms(){
	if (!time40ms){
		myButton.value = HAL_GPIO_ReadPin(SW0_GPIO_Port, SW0_Pin);
		checkMEF(&myButton);
		buttonTask(&myButton);
		time40ms = 4;
	}
	else
	{
		time40ms--;
	}
}


void do100ms(){
	if (!time100ms){
		stateTask();
		heartbeatTask();
		datosComSerie.bufferRx[datosComSerie.indexWriteRx+NBYTES]=ANALOG_IR;
		datosComSerie.indexStart=datosComSerie.indexWriteRx;
		decodeData(&datosComSerie);
		time100ms = 10;
	}
	else
	{
		time100ms--;
	}
}


void do500ms(){
	if (!time500ms){
		datosComWIFI.bufferRx[datosComWIFI.indexWriteRx+NBYTES]=ALIVE;
		datosComWIFI.indexStart=datosComWIFI.indexWriteRx;
		decodeData(&datosComWIFI);
		time500ms = 50;
	}
	else
	{
		time500ms--;
	}
}


void decodeProtocol(_sDato *datosCom){
    static uint8_t nBytes=0;
    uint8_t indexWriteRxCopy=datosCom->indexWriteRx;

    while (datosCom->indexReadRx!=indexWriteRxCopy)
    {
        switch (estadoProtocolo) {
            case START:
                if (datosCom->bufferRx[datosCom->indexReadRx++]=='U'){
                    estadoProtocolo=HEADER_1;
                    datosCom->checksumRx=0;
                }
                break;
            case HEADER_1:
                if (datosCom->bufferRx[datosCom->indexReadRx++]=='N')
                   estadoProtocolo=HEADER_2;
                else{
                    datosCom->indexReadRx--;
                    estadoProtocolo=START;
                }
                break;
            case HEADER_2:
                if (datosCom->bufferRx[datosCom->indexReadRx++]=='E')
                    estadoProtocolo=HEADER_3;
                else{
                    datosCom->indexReadRx--;
                   estadoProtocolo=START;
                }
                break;
        case HEADER_3:
            if (datosCom->bufferRx[datosCom->indexReadRx++]=='R')
                estadoProtocolo=NBYTES;
            else{
                datosCom->indexReadRx--;
                estadoProtocolo=START;
            }
            break;
            case NBYTES:
                datosCom->indexStart=datosCom->indexReadRx;
                nBytes=datosCom->bufferRx[datosCom->indexReadRx++];
                estadoProtocolo=TOKEN;
                break;
            case TOKEN:
                if (datosCom->bufferRx[datosCom->indexReadRx++]==':'){
                   estadoProtocolo=PAYLOAD;
                    datosCom->checksumRx ='U'^'N'^'E'^'R'^ nBytes^':';
                }
                else{
                    datosCom->indexReadRx--;
                    estadoProtocolo=START;
                }
                break;
            case PAYLOAD:
                if (nBytes>1){
                    datosCom->checksumRx ^= datosCom->bufferRx[datosCom->indexReadRx++];
                }
                nBytes--;
                if(nBytes<=0){
                    estadoProtocolo=START;
                    if(datosCom->checksumRx == datosCom->bufferRx[datosCom->indexReadRx]){
                        decodeData(datosCom);
                    }
                }
                break;
            default:
                estadoProtocolo=START;
                break;
        }
    }
}


void decodeData(_sDato *datosCom){
    uint8_t auxBuffTx[50], indiceAux=0, checksum;

    auxBuffTx[indiceAux++]='U';
    auxBuffTx[indiceAux++]='N';
    auxBuffTx[indiceAux++]='E';
    auxBuffTx[indiceAux++]='R';
    auxBuffTx[indiceAux++]=0;
    auxBuffTx[indiceAux++]=':';
    auxBuffTx[indiceAux++]='9';
    auxBuffTx[indiceAux++]='7';

    switch (datosCom->bufferRx[datosCom->indexStart+NBYTES]) {
        case ALIVE:
            auxBuffTx[indiceAux++] = ALIVE;
            auxBuffTx[indiceAux++] = ACKNOWLEDGE;
            auxBuffTx[NBYTES] = 0x05;
            break;
        case FIRMWARE:
        	auxBuffTx[indiceAux++] = FIRMWARE;
			auxBuffTx[NBYTES] = 0x04;
        	break;
        case ANALOG_IR:
        	auxBuffTx[indiceAux++] = ANALOG_IR;
        	for (uint8_t c=0; c<NUMCHANNELSADC; c++)					// For all 8 channels
			{
				for (uint8_t i=2; i<SIZEBUFADC; i++)					// Scans 64 bytes of data
				{
					datosADC.dynSumDF[c] = 0;							// Clear buffer
					datosADC.dynSumDF[c] += datosADC.bufADC[i][c];		// Add value "i"
					datosADC.dynSumDF[c] += datosADC.bufADC[i-1][c];	// Add value "i-1"
					datosADC.dynSumDF[c] += datosADC.bufADC[i-2][c];	// Add value "i-2"
					datosADC.bufADC[i][c] = (datosADC.dynSumDF[c]/3);	// Calculates average
				}
				myADCbuf[c].u16 = datosADC.bufADC[SIZEBUFADC-1][c];		// Save average value
				auxBuffTx[indiceAux++] = myADCbuf[c].u8[0];				// Send low byte
				auxBuffTx[indiceAux++] = myADCbuf[c].u8[1];				// Send high byte
			}
			auxBuffTx[NBYTES] = (0x04)+(0x10); 							// Sends 0x04 + 16 BYTES
			break;
        default:
			auxBuffTx[indiceAux++] = UNKNOWNCOMMAND;
			auxBuffTx[NBYTES] = 0x04;
			break;
	}
    checksum = 0;
	for(uint8_t a=0; a<indiceAux; a++)
	{
		checksum ^= auxBuffTx[a];
		datosCom->bufferTx[datosCom->indexWriteTx++] = auxBuffTx[a];
	}
	datosCom->bufferTx[datosCom->indexWriteTx++] = checksum;

}


void communicationTask(_sDato *datosCom, uint8_t source){
	if (datosCom->indexReadRx != datosCom->indexWriteRx)
		decodeProtocol(datosCom);

	// FOR USART & WIFI COMMUNICATION
	if (datosCom->indexReadTx != datosCom->indexWriteTx){
		if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TXE)){
			USART1->DR = datosCom->bufferTx[datosCom->indexReadTx++];
		}
	}

	// FOR USB COMMUNICATION
	if (datosCom->newData == true){
		if ((CDC_Transmit_FS(datosComUSB.bufferRx, datosComUSB.indexWriteRx)) == USBD_OK){
			datosCom->newData = false;
		}
	}
	/*
	if (source == viaUART){
		if (datosCom->indexReadTx != datosCom->indexWriteTx){
			if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TXE)){
				USART1->DR = datosCom->bufferTx[datosCom->indexReadTx++];
			}
		}
	}
	else if (source == viaUSB){
		if (datosCom->newData == true){
			if ((CDC_Transmit_FS(datosComUSB.bufferRx, datosComUSB.indexWriteRx)) == USBD_OK){
				datosCom->newData = false;
			}
		}
	}
	*/
}


void buttonTask(_sButton *button){
	switch (button->estado)
	{
		case DOWN:
			HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, 0);	// ON
			break;
		case RISING:
			mode++;
			if (mode == maxMODES)
				mode = 0;						// Increase mode (circular: 0-MAX)
			break;
		default:
			break;
	}
}


void stateTask(){
	switch (mode)
	{
		case modeIDLE:
			myHB = maskIDLE;
			break;
		case modeONE:
			myHB = maskONE;
			break;
		case modeTWO:
			myHB = maskTWO;
			break;
		default:
			break;
	}
}


void init_myTCRT5000(){
	uint8_t initialValues[20] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19};
	for (uint8_t i=0;i<20;i++)
	{
		datosTCRT5000[0].distanceValues[i] = initialValues[i];
	}
}

void ESP01DoCHPD(uint8_t value){
	HAL_GPIO_WritePin(CH_EN_GPIO_Port, CH_EN_Pin, value);
}

void ESP01ChangeState(_eESP01STATUS esp01State){
	switch((uint32_t)esp01State){
	case ESP01_WIFI_CONNECTED:
		myHB = maskIDLE;
		break;
	case ESP01_UDPTCP_CONNECTED:
		myHB = 0xA000A000;
		break;
	case ESP01_UDPTCP_DISCONNECTED:
		myHB = 0xA080A080;
		break;
	case ESP01_WIFI_DISCONNECTED:
		myHB = 0xAAAA0000;
		break;
	}
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_TIM1_Init();
  MX_USART1_UART_Init();
  MX_USB_DEVICE_Init();
  MX_ADC1_Init();
  MX_ADC2_Init();
  MX_TIM4_Init();
  /* USER CODE BEGIN 2 */
  HAL_TIM_Base_Start_IT(&htim1);
  CDC_AttachRxData(USB_Receive);
  inicializarBoton(&myButton);

  HAL_UART_Receive_IT(&huart1, &datosComSerie.bufferRx[datosComSerie.indexWriteRx], 1);
  HAL_UART_Receive_IT(&huart1, &datosComWIFI.bufferRx[datosComWIFI.indexWriteRx], 1);
  HAL_ADC_Start_DMA(&hadc1, (uint32_t *)datosADC.bufADC, NUMCHANNELSADC);

  ESP01_Init(&esp01);
  ESP01_SetWIFI(WIFI_SSID, WIFI_PASSWORD);
  ESP01_StartUDP(WIFI_UDP_REMOTEIP, WIFI_UDP_REMOTEPORT, WIFI_UDP_LOCALPORT);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	  if (!time10ms)						// Every 10 milliseconds
	  {
		  do10ms();
		  do40ms();
		  do100ms();
		  do500ms();
	  }
	  communicationTask(&datosComSerie, viaUART);
	  communicationTask(&datosComWIFI, viaWIFI);
	  communicationTask(&datosComUSB, viaUSB);
	  ESP01_Task();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC|RCC_PERIPHCLK_USB;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 8;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_13CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_2;
  sConfig.Rank = ADC_REGULAR_RANK_3;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_3;
  sConfig.Rank = ADC_REGULAR_RANK_4;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_4;
  sConfig.Rank = ADC_REGULAR_RANK_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_5;
  sConfig.Rank = ADC_REGULAR_RANK_6;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_6;
  sConfig.Rank = ADC_REGULAR_RANK_7;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_7;
  sConfig.Rank = ADC_REGULAR_RANK_8;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief ADC2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC2_Init(void)
{

  /* USER CODE BEGIN ADC2_Init 0 */

  /* USER CODE END ADC2_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC2_Init 1 */

  /* USER CODE END ADC2_Init 1 */

  /** Common config
  */
  hadc2.Instance = ADC2;
  hadc2.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc2.Init.ContinuousConvMode = DISABLE;
  hadc2.Init.DiscontinuousConvMode = DISABLE;
  hadc2.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc2.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc2.Init.NbrOfConversion = 1;
  if (HAL_ADC_Init(&hadc2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_2;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
  if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC2_Init 2 */

  /* USER CODE END ADC2_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 71;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 250;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */

}

/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 0;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 65535;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */
  HAL_TIM_MspPostInit(&htim4);

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(CH_EN_GPIO_Port, CH_EN_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : LED_Pin */
  GPIO_InitStruct.Pin = LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PB10 PB11 */
  GPIO_InitStruct.Pin = GPIO_PIN_10|GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : SW0_Pin */
  GPIO_InitStruct.Pin = SW0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(SW0_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : CH_EN_Pin */
  GPIO_InitStruct.Pin = CH_EN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(CH_EN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PB4 PB5 */
  GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure peripheral I/O remapping */
  __HAL_AFIO_REMAP_TIM3_PARTIAL();

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
