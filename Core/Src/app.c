/*
 * app.c
 *
 *  Created on: May 3, 2021
 *      Author: ajg1079
 */
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "eth.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "usb_otg.h"
#include "gpio.h"

#include <string.h>

#include "font.h"

#define	PACKETLIMITELENGTH	256

#define WIDTH	128
#define HIGH	32

#define DEBUGLOG	1

#define COOLERAUTOMODE	0
#define COOLERUSERMODE	1

#define SSD1306_LCDWIDTH			128
#define SSD1306_LCDHEIGHT			32
#define SSD1306_SETCONTRAST			0x81
#define SSD1306_DISPLAYALLON_RESUME 0xA4
#define SSD1306_DISPLAYALLON		0xA5
#define SSD1306_NORMALDISPLAY		0xA6
#define SSD1306_INVERTDISPLAY		0xA7
#define SSD1306_DISPLAYOFF			0xAE
#define SSD1306_DISPLAYON			0xAF
#define SSD1306_SETDISPLAYOFFSET	0xD3
#define SSD1306_SETCOMPINS			0xDA
#define SSD1306_SETVCOMDETECT		0xDB
#define SSD1306_SETDISPLAYCLOCKDIV	0xD5
#define SSD1306_SETPRECHARGE		0xD9
#define SSD1306_SETMULTIPLEX		0xA8
#define SSD1306_SETLOWCOLUMN		0x00
#define SSD1306_SETHIGHCOLUMN		0x10
#define SSD1306_SETSTARTLINE		0x40
#define SSD1306_MEMORYMODE			0x20
#define SSD1306_COLUMNADDR			0x21
#define SSD1306_PAGEADDR			0x22
#define SSD1306_COMSCANINC			0xC0
#define SSD1306_COMSCANDEC			0xC8
#define SSD1306_SEGREMAP			0xA0
#define SSD1306_CHARGEPUMP			0x8D
#define SSD1306_EXTERNALVCC			0x1
#define SSD1306_SWITCHCAPVCC		0x2

// SW 인터럽트 누를때마다 바뀔때 적용되는 변수. 동작모드
int iMode;

// uart 수신 버퍼 index
int iUartRxCallbackIndex = 0;

uint8_t sUART_DMA_ReceiveBuffer[PACKETLIMITELENGTH];
uint8_t gpubuffer[512] = "";

void ssd1306_W_Command(uint8_t c);
void init_display(void);
void ssd1306_drawingbuffer(char *sdata);
void fDisplayChar(int iLocationX, int iLocationY, char cData, char *displaybuffer);
void fDisplayString(int iLocationX, int iLocationY, char *displaybuffer, const char *p, ...);
int fCompareFunction(char *source, char *target, int iSize);
int fConvertStringToInt32(char *source, int sourcesize);
int GetADCtable(int iSourceData);
int GetAutotable(int iSourceData);

/*
 *  @brief	Application Main Code
 *  @param	None
 *  @retval	None
 */
void main_app_function(void)
{
	int x,y,i;

	//명령어 마지막 부분 index
	int iCommandEndIndex = 0;
	//int형으로 변환하기 위한 임시변수
	int iTemp = 0;

	char sDisplayLogData[32] ="";
	char sTempStringData[16] = "";

	int iCoolerSpeed = 0;
	int iCoolerSpeedFromADC = 0;
	int iCoolerSpeedFromCMD = 0;

	uint32_t ADC_Data[2];

	char command[8] = "";

	printf("main_app_function Start.\r\n");

	// 기본 동작 모드는 Auto Mode
	iMode = COOLERAUTOMODE;

	// PWM (Timer)
	uint32_t htim1_duty_value = 0;

	HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4);
	__HAL_TIM_SetCompare(&htim1, TIM_CHANNEL_4, htim1_duty_value);

	//i2C OLED init
	init_display();

	//그래픽 버퍼 초기화
	memset(gpubuffer,0x00,sizeof(gpubuffer));

	fDisplayString(0,0,gpubuffer,"Hello world!");
	ssd1306_drawingbuffer(gpubuffer);

	memset(sUART_DMA_ReceiveBuffer,0x00,sizeof(sUART_DMA_ReceiveBuffer));
	iUartRxCallbackIndex = 0;

	while(1)
	{
		// ADC get data
		HAL_ADC_Start(&hadc1);
		HAL_ADC_PollForConversion(&hadc1,10);
		ADC_Data[0] = HAL_ADC_GetValue(&hadc1);

		//ADC 정보 기반으로 구성한 쿨러스피드 테이블 읽기
		iCoolerSpeedFromADC = GetADCtable(ADC_Data[0]);

		for (x = PACKETLIMITELENGTH; x >= 0; x--)
		{
			if((sUART_DMA_ReceiveBuffer[x-1] == '\r')&&(sUART_DMA_ReceiveBuffer[x] == '\n'))
			{
				//command 분석
				// ']' 위치 파악
				iCommandEndIndex = 0;
				for( i = 0 ; i < x ; i++)
				{
					if( sUART_DMA_ReceiveBuffer[i] == ']' )
					{
						iCommandEndIndex = i;
						memcpy(command,sUART_DMA_ReceiveBuffer + 1,iCommandEndIndex-1);
					}
				}

				//온도 정보
				if(fCompareFunction(command,"TEMP",4)==0)
				{
					//Get Data
					memcpy(sTempStringData,sUART_DMA_ReceiveBuffer + (iCommandEndIndex + 1),3);
					//string -> int32
					iTemp = fConvertStringToInt32(sTempStringData,3);
					// 온도에 따른 쿨러 속도를 테이블화 하여 조절
					iCoolerSpeedFromCMD = GetAutotable(iTemp);
				}
				//Reset 신호
				else if(fCompareFunction(command,"RESET",5)==0)
				{
					// soft reset
					NVIC_SystemReset();
				}

				// UART수신 인터럽트 버퍼 초기화
				memset(sUART_DMA_ReceiveBuffer,0x00,sizeof(sUART_DMA_ReceiveBuffer));
				iUartRxCallbackIndex = 0;
			}
		}

		// 동작 모드를 Auto로 설정
		if(iMode == COOLERAUTOMODE)
		{
			iCoolerSpeed = iCoolerSpeedFromCMD ;
			fDisplayString(0,2,gpubuffer,"Auto Mode");
		}
		// 동작 모드를 User으로 설정
		else if(iMode == COOLERUSERMODE)
		{
			iCoolerSpeed = iCoolerSpeedFromADC ;
			fDisplayString(0,2,gpubuffer,"User Mode");
		}

		// PWM 최종 설정
		__HAL_TIM_SetCompare(&htim1, TIM_CHANNEL_4, iCoolerSpeed);

		//OLED에 Cooler speed 디스플레이
		memset(sDisplayLogData,0x00,sizeof(sDisplayLogData));
		sprintf(sDisplayLogData,"CoolerSPD=%3d%",iCoolerSpeed/10);
		fDisplayString(0,0,gpubuffer,sDisplayLogData);

		//Update GPU buffer
		ssd1306_drawingbuffer(gpubuffer);
	}
}

/*
 *  @brief	i2C OLED Command 전송
 *  @param	c	1Byte Command
 *  @retval	None
 */
void ssd1306_W_Command(uint8_t c)
{
    HAL_StatusTypeDef res;

    uint8_t buffer[2]={0};		//Control Byte + Command Byte
    buffer[0]=(0<<7)|(0<<6);	//Co=0 , D/C=0
    buffer[1]=c;

    res = HAL_I2C_Master_Transmit(&hi2c2,(uint16_t)(0x3C)<<1,(uint8_t*)buffer,2,9999);
    if(res != HAL_OK)
    {
    	printf("Err:%d\r\n",res);
    }

	while (HAL_I2C_GetState(&hi2c1) != HAL_I2C_STATE_READY);
}

/*
 *  @brief	i2C OLED Command 전송
 *  @param	data_buffer	전송 할 데이터 배열 시작 주소
 *			buffer_size	전송 할 Data 크기
 *  @retval	None
 */
void ssd1306_W_Data(uint8_t* data_buffer, uint16_t buffer_size)
{
	HAL_StatusTypeDef res;
	res = HAL_I2C_Mem_Write(&hi2c2,(uint16_t)(0x3C<<1),0x40,1,data_buffer,buffer_size,9999);
	if(res != HAL_OK)
	{
		printf("Err:%d\r\n",res);
	}

	while (HAL_I2C_GetState(&hi2c2) != HAL_I2C_STATE_READY);
}

/*
 *  @brief	i2C OLED 초기화 함수
 *  @param	None
 *  @retval	None
 */
void init_display(void)
{
    // Init sequence for 128x64 OLED module
	ssd1306_W_Command(0xAE);		// 0xAE
    ssd1306_W_Command(0xD5);		// 0xD5
    ssd1306_W_Command(0x80);		// the suggested ratio 0x80
    ssd1306_W_Command(0xA8);		// 0xA8
    ssd1306_W_Command(0x3F);
    ssd1306_W_Command(0xD3);		// 0xD3
    ssd1306_W_Command(0x00);		// no offset
    ssd1306_W_Command(0x40);		// line #0
    ssd1306_W_Command(0x8D); 		// 0x8D
    ssd1306_W_Command(0x14); 		// using internal VCC
    ssd1306_W_Command(0x20);		// 0x20
    ssd1306_W_Command(0x00);		// 0x00 horizontal addressing
    ssd1306_W_Command(0xA0 | 0x1); 	// rotate screen 180
    ssd1306_W_Command(0xC8); 		// rotate screen 180
    ssd1306_W_Command(0xDA);    	// 0xDA
    ssd1306_W_Command(0x02);
    ssd1306_W_Command(0x81);		// 0x81
    ssd1306_W_Command(0xCF);
    ssd1306_W_Command(0xd9); 		// 0xd9
    ssd1306_W_Command(0xF1);
    ssd1306_W_Command(0xDB);		// 0xDB
    ssd1306_W_Command(0x40);
    ssd1306_W_Command(0xA4);		// 0xA4
    ssd1306_W_Command(0xA6); 		// 0xA6
    ssd1306_W_Command(0xAF);		//switch on OLED
}

/*
 *  @brief	i2C OLED에 그래픽 버퍼 전달 함수
 *  @param	sdata	그래픽 버퍼 시작 주소
 *  @retval	None
 */
void ssd1306_drawingbuffer(char *sdata)
{
	ssd1306_W_Command(0x00);
	ssd1306_W_Command(0x10);

	// 사용하는 OLED는 128*64드라이버IC를 탑재 했지만 OLED 자체는 128*32로
	//드라이버 데이터 시트상 행이 8구역으로 나뉘어 있고 OLED 연결은 테스트 결과
	// 0~3이 아닌 4~7에 연결 되어 있다
	for(uint8_t i=4;i<8;i++)
	{
		ssd1306_W_Command(0xB0+i);
		ssd1306_W_Data(sdata + (128 * (i-4)),128);
	}
}

/*
 *  @brief	display 버퍼에 문자 삽입 역할 함수
 *  @param	iLocationX		표기를 시작할 X 좌표
 *			iLocationY		표기를 시작할 Y 좌표
 *			cData			표기 할 문자
 *			displaybuffer	디스플레이 버퍼 배열 시작 주소
 *  @retval	None
 */
void fDisplayChar(int iLocationX, int iLocationY, char cData, char *displaybuffer)
{
	/*
	 *	사용하는 폰트는 세로가 바이트로 구별 (개별 앨리먼트)되어 있는데 가로는 비트단위로
	 *	제작되어 있다. 문제는 사용하는 OLED 드라이버칩 특성상 가로가 바이트 단위고 세로가
	 *	비트 단위라 변환해줘야 한다
	 */

	// 입력하는 문자를 사용하는 폰트 데이터 index에 맞게 변환
	int iIndexChar = (cData - 32) * 12;
	//For문 사용 변수
	int x, y, z;
	// 실제 좌표와 드라이버칩의 메모리 위치 환산 위한 변수
	int iLocationSumY = 0;
	char buf = 0b00000001;
	char nbuf = 0b00000001;

	//특이하게 byte by byte로 픽셀이 매칭이 아닌 행은 Bit로, 열은 Byte로 구분 되어 있다.
	//참고 자료 : http://www.datasheet.kr/ic/1017173/SSD1309-datasheet-pdf.html

	// 폰트 세로크기
	for (y = 0; y < 12; y++)
	{
		// 폰트 가로크기
		for (x = 0; x < 1; x++)
		{
			//폰트 가로는 비트로 구성
			for (z = 7; z >= 0; z--)
			{
				//폰트 가로축을 1비트씩 읽어서 On해야 되는 픽셀이 있다면
				if (((Font12_Table[iIndexChar + (y + x)] >> z) & 0x1) == 1)
				{
					// 문자의 시작하려는 y좌표와 현재 읽어들인 문자의 y좌표 합 계산
					//0~31까지의 실제 OLED 좌표와 드라이버칩 메모리 위치 환산 하기 위해 계산해야 한다
					iLocationSumY = y + iLocationY;

					//즉 iLocationSumY가 0~7,8~15,16~23,24~31 각각 메모리상으로는 1개의 char 형태로
					//묶여있는 형태다
					if(iLocationSumY < 8)
					{
						//각 영역 내부에 표시하기 위한 shift
						buf = 0b00000001 << iLocationSumY;
						//버퍼 메모리에 해당 위치 변환된 부분에 1을 기록
						displaybuffer[(iLocationY*128)+(iLocationX+(7-z))]=displaybuffer[(iLocationY*128)+(iLocationX+(7-z))] | buf;
					}
					else if((iLocationSumY >= 8)&&(iLocationSumY < 16))
					{
						iLocationSumY = iLocationSumY -8;
						buf = 0b00000001 << iLocationSumY;
						displaybuffer[((iLocationY+1)*128)+(iLocationX+(7-z))]=displaybuffer[((iLocationY+1)*128)+(iLocationX+(7-z))] | buf;
					}
					else if((iLocationSumY >= 16)&&(iLocationSumY < 24))
					{
						iLocationSumY = iLocationSumY -16;
						buf = 0b00000001 << iLocationSumY;
						displaybuffer[((iLocationY+2)*128)+(iLocationX+(7-z))]=displaybuffer[((iLocationY+2)*128)+(iLocationX+(7-z))] | buf;
					}
					else if((iLocationSumY >= 24)&&(iLocationSumY < 32))
					{
						iLocationSumY = iLocationSumY -24;
						buf = 0b00000001 << iLocationSumY;
						displaybuffer[((iLocationY+3)*128)+(iLocationX+(7-z))]=displaybuffer[((iLocationY+3)*128)+(iLocationX+(7-z))] | buf;
					}
				}
				// 0으로 처리되는 픽셀이면
				else if (((Font12_Table[iIndexChar + (y + x)] >> z) & 0x1) == 0)
				{
					iLocationSumY = y + iLocationY;
					if(iLocationSumY < 8)
					{
						buf = 0b00000001 << iLocationSumY;
						// 해당 부분 픽셀은 Off이므로 Not연산으로 비트 반전
						nbuf = ~buf;
						//And연산으로 꺼버린다
						displaybuffer[(iLocationY*128)+(iLocationX+(7-z))]=(displaybuffer[(iLocationY*128)+(iLocationX+(7-z))] | buf) & nbuf ;
					}
					else if((iLocationSumY >= 8)&&(iLocationSumY < 16))
					{
						iLocationSumY = iLocationSumY -8;
						buf = 0b00000001 << iLocationSumY;
						nbuf = ~buf;
						displaybuffer[((iLocationY+1)*128)+(iLocationX+(7-z))]=(displaybuffer[((iLocationY+1)*128)+(iLocationX+(7-z))] | buf) & nbuf;
					}
					else if((iLocationSumY >= 16)&&(iLocationSumY < 24))
					{
						iLocationSumY = iLocationSumY -16;
						buf = 0b00000001 << iLocationSumY;
						nbuf = ~buf;
						displaybuffer[((iLocationY+2)*128)+(iLocationX+(7-z))]=(displaybuffer[((iLocationY+2)*128)+(iLocationX+(7-z))] | buf) & nbuf;
					}
					else if((iLocationSumY >= 24)&&(iLocationSumY < 32))
					{
						iLocationSumY = iLocationSumY -24;
						buf = 0b00000001 << iLocationSumY;
						nbuf = ~buf;
						displaybuffer[((iLocationY+3)*128)+(iLocationX+(7-z))]=(displaybuffer[((iLocationY+3)*128)+(iLocationX+(7-z))] | buf) & nbuf;
					}
				}
			}
		}
	}
}

/*
 *  @brief	display에 string 뿌리기 위한 함수
 *  @param	iLocationX		표기를 시작할 X 좌표
 *			iLocationY		표기를 시작할 Y 좌표
 *			displaybuffer	디스플레이 버퍼 배열 시작 주소
 *			p				문자열 주소
 *  @retval	None
 */
void fDisplayString(int iLocationX, int iLocationY, char* displaybuffer, const char* p, ...)
{
	// 문자열 내부 행 좌표
	int x = 0;
	// NULL문자 나올때까비 반복
	while (*p != '\0')
	{
		fDisplayChar(iLocationX + x, iLocationY, *p, displaybuffer);
		//문자"열"이므로 다음 문자는 폰트의 가로 크기인 8만큼 더해준다
		x = x + 8;
		//다음 문자 호출
		p++;
	}
}

/*
 *  @brief  문자열 비교 함수
 *  @param  source 비교할 문자열 시작 주소
 *          target 비교대상 문자열 시작 주소
 *          iSize  비교할 길이
 *  @retval 비교 대상 결과 값, 같으면 0 다르면 -1
 */
int fCompareFunction(char *source, char *target, int iSize)
{
	for(int i = 0 ; i < iSize ; i++ )
	{
		if(source[i] != target[i])
		{
			return -1;
		}
	}
	return 0;
}

/*
 *  @brief  문자열 형태의 숫자를 int32값으로 변환
 *  @param  source  문자열 에서 정수형으로 바꿀 문자열 시작 주소
 *  @retval int32로 변환 된 값
 */
int fConvertStringToInt32(char *source, int sourcesize)
{
	int buf = source[0] - 0x30;
	int res = 0;

	int i;
	res = res + buf;

	for ( i = 1 ; i < sourcesize ; i++)
	{
		res = res * 10;
		buf = source[i] - 0x30;
		res = res + buf;
	}

	return res;
}

/*
 *  @brief	UART로 부터 받은 온도 데이터를 테이블화 하여 출력.
 *  @param	iSourceData	가공하지 않은 ADC 데이터
 *  @retval PWM에 적용할 테이블 데이터
 */
int GetAutotable(int iSourceData)
{
	int res = 0;

	if(iSourceData > 310)
	{
		res = 900;
	}
	else if((iSourceData > 300)&&(iSourceData <= 310))
	{
		res = 700;
	}
	else if((iSourceData > 290)&&(iSourceData <= 300))
	{
		res = 500;
	}
	else if(iSourceData <= 290)
	{
		res = 400;
	}

	return res;
}

/*
 *  @brief	ADC로부터 읽어들인 정보를 Cooler Speed에 맞게 테이블화 하여 출력
 *  @param	iSourceData	가공하지 않은 ADC 데이터
 *  @retval	PWM에 적용할 테이블 데이터
 */
int GetADCtable(int iSourceData)
{
	int res = 0;

	if(iSourceData < 410)
	{
		res = 100;
	}
	else if((iSourceData >= 410)&&(iSourceData < 820))
	{
		res = 200;
	}
	else if((iSourceData >= 820)&&(iSourceData < 1229))
	{
		res = 300;
	}
	else if((iSourceData >= 1229)&&(iSourceData < 1638))
	{
		res = 400;
	}
	else if((iSourceData >= 1638)&&(iSourceData < 2048))
	{
		res = 500;
	}
	else if((iSourceData >= 2048)&&(iSourceData < 2457))
	{
		res = 600;
	}
	else if((iSourceData >= 2457)&&(iSourceData < 2867))
	{
		res = 700;
	}
	else if((iSourceData >= 2867)&&(iSourceData < 3276))
	{
		res = 800;
	}
	else if((iSourceData >= 3276)&&(iSourceData < 3686))
	{
		res = 900;
	}
	else if((iSourceData >= 3686)&&(iSourceData < 4096))
	{
		res = 1000;
	}

	return res;
}

/*
 *  @brief	UART 수신 인터럽트로부터 데이터 받아와 호출 할때마다 index 증가하여 배열에 저장.
 *  @param	data	intterupt쪽에서 데이터 받아오기 위한 파라미터
 *  @retval	None
 */
void getRxBuffer(uint8_t data)
{
	sUART_DMA_ReceiveBuffer[iUartRxCallbackIndex] = data;
	iUartRxCallbackIndex++;
}
