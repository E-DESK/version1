/**
 *	Keil project for USART using strings
 *
 *  Before you start, select your target, on the right of the "Load" button
 *
 *	@author		Tilen Majerle
 *	@email		tilen@majerle.eu
 *	@website	http://stm32f4-discovery.com
 *	@ide		Keil uVision 5
 *	@packs		STM32F4xx Keil packs version 2.2.0 or greater required
 *	@stdperiph	STM32F4xx Standard peripheral drivers version 1.4.0 or
 *greater required
 */
#include "main.h"
/*PID*/
#define PID_PARAM_KP        100         /* Proporcional */
#define PID_PARAM_KI        0.025       /* Integral */
#define PID_PARAM_KD        20          /* Derivative */
#define LIGHT_CURRENT       lights[1]
#define LIGHT_WANT          lights[0]
#define LIGHT_WANT_VALUE    100
float lights[2];
float lightWantValue = LIGHT_WANT_VALUE;
/*
RTOS TASK
*/
#define STACK_SIZE_MIN 128 /* usStackDepth	- the stack size DEFINED IN \
                              WORDS.*/
uint8_t light;
/*xTaskHandle*/
xTaskHandle ptr_readSensor;
xTaskHandle ptr_showLCD;
xTaskHandle ptr_response;
/**/
static void vDA2_ReadSensor(void *pvParameters);
static void vDA2_ShowLCD   (void *pvParameters);
static void vDA2_Response  (void *pvParemeters);

/*LCD functions*/
int mLCD_resetLcd(void);
int mLCD_showTitle(void);
int mLCD_showDateTime(void);
int mLCD_showSensor(void);
char note[100]={0};

typedef struct STRUCT_OF_NOTE
{
    TM_DS1307_Time_t noteTime ;
    char note[50];
}NOTE;

float getDistance(uint16_t voltaValue);
NOTE parsingLine(char* inputString);

Env InRoomEvn;
USERDATA_TYPE userData;



NOTE userNotes[10];
int noteIndex=0;
/*Global PWM PID*/
/* Timer data for PWM */
TM_PWM_TIM_t TIM_Data;
char buf[150];
uint8_t devices, i, count;
/* PID error */
float pid_error;
/* Duty cycle for PWM */
float duty = 0;
float currentDuty =0;
/* ARM PID Instance, float_32 format */
arm_pid_instance_f32 PID;

int main() 
{
    initMain(1);
    if(initTask())
    {
        vTaskStartScheduler();
    }
    for(;;);
}

int initMain(int flag)
{
    if(flag)
    {
        SystemInit();

        TM_DISCO_LedInit();
        TM_DISCO_ButtonInit();
        TM_DELAY_Init();
        
        LCD_Init();

        I2C1_Init();
        /*I2C1_PINS PACK 2
        SCL=PB8_SDA=PB9  */
        BH1750_Init();

        DHT_GetTemHumi();
        
        /*Sensor khoang cach*/
        /*ADC: Pin_A0 (ADC1 channel 0)*/
        TM_ADC_Init(ADC1,ADC_Channel_0);

        mLCD_resetLcd();
        LCD_Clear(BLACK);
        mLCD_showTitle();
        
        TM_USART_Init(USART6, TM_USART_PinsPack_1, 9600);
        TM_USART_SetCustomStringEndCharacter(USART6,'.');
        
        /*I2C2_PINS PACK 1
        SCL=PB10_SDA=PB11  */
        if (TM_DS1307_Init() != TM_DS1307_Result_Ok)
        {
            /*Warning that RTC error*/
            TM_DISCO_LedOn(LED_ALL);
        }
            
        if(1)
        {
            /* Set date and time */
            /* Day 7, 26th May 2014, 02:05:00 */
            time.hours = 16;
            time.minutes = 33;
            time.seconds = 20;
            time.date = 11;
            time.day = 8;
            time.month = 9;
            time.year = 16;
            TM_DS1307_SetDateTime(&time);

            /* Disable output first */
            TM_DS1307_DisableOutputPin();

            /* Set output pin to 4096 Hz */
            TM_DS1307_EnableOutputPin(TM_DS1307_OutputFrequency_4096Hz);
        }
        
        TM_DS1307_GetDateTime(&time);
            
        mLCD_resetLcd();
        
        InRoomEvn.AnhSang = 0;
        InRoomEvn.DoAmKhi = 0;
        InRoomEvn.DoAmKhi = 0;
        
        /*PWM and PID*/
        if(1)
        {

        	/* Set PID parameters */
        /* Set this for your needs */
        PID.Kp = PID_PARAM_KP;		/* Proporcional */
        PID.Ki = PID_PARAM_KI;		/* Integral */
        PID.Kd = PID_PARAM_KD;		/* Derivative */

        /* Initialize PID system, float32_t format */
        arm_pid_init_f32(&PID, 1);
            
        /* Initialize TIM2, 1kHz frequency */
        TM_PWM_InitTimer(TIM2, &TIM_Data, 1000);

        /* Initialize TIM2, Channel 1, PinsPack 2 = PA5 */
        TM_PWM_InitChannel(TIM2, TM_PWM_Channel_1, TM_PWM_PinsPack_2);

        /* Set default duty cycle */
        TM_PWM_SetChannelPercent(TIM2, &TIM_Data, TM_PWM_Channel_1, duty);
        }
    }
    return 1;
}

int initTask()
{
    xTaskCreate(vDA2_ReadSensor,(const signed char*)"vDA2_ReadSensor",STACK_SIZE_MIN,NULL,tskIDLE_PRIORITY+3,&ptr_readSensor);
    xTaskCreate(vDA2_ShowLCD, (const signed char *)"vDA2_ShowLCD", STACK_SIZE_MIN,NULL, tskIDLE_PRIORITY+1,&ptr_showLCD);
    xTaskCreate(vDA2_Response, (const signed char *)"vDA2_Response", STACK_SIZE_MIN,NULL, tskIDLE_PRIORITY+1,&ptr_response);
    return 1;
}

static void vDA2_Response(void *pvParameters)
{
    for(;;)
    {
        /* Calculate error */
        LIGHT_CURRENT = InRoomEvn.AnhSang;
        LIGHT_WANT = lightWantValue;
        pid_error = LIGHT_CURRENT - LIGHT_WANT;

        /* Calculate PID here, argument is error */
        /* Output data will be returned, we will use it as duty cycle parameter */
        currentDuty = duty;
        duty = arm_pid_f32(&PID, pid_error);

        /* Check overflow, duty cycle in percent */
        if (duty > 1000) {
        duty = 1000;
        } else if (duty < -1000) {
        duty = -1000;
        }
        
        if(pid_error >= 0)
        {
            TM_PWM_SetChannelPercent(TIM2, &TIM_Data, TM_PWM_Channel_1, currentDuty - duty);
        }
        else if(pid_error < 0)
        {
            /* Set PWM duty cycle for LED*/
            TM_PWM_SetChannelPercent(TIM2, &TIM_Data, TM_PWM_Channel_1, currentDuty + (duty*(-1)));
        }
    }
}

static void vDA2_ReadSensor(void *pvParameters)
{
    for(;;)
    {
        vTaskDelay( 250 / portTICK_RATE_MS );
        
        int index =0;
        DHT_GetTemHumi();
        InRoomEvn.AnhSang = BH1750_Read();
        InRoomEvn.DoAmKhi = DHT_doam();
        InRoomEvn.NhietDo = DHT_nhietdo();
        
        userData.distance = getDistance(TM_ADC_Read(ADC1,ADC_Channel_0));
        
        TM_DS1307_GetDateTime(&time);
        TM_DISCO_LedToggle(LED_ORANGE);
        
        if(!TM_USART_BufferEmpty(USART6))
        {
            TM_USART_Gets(USART6,note,TM_USART6_BUFFER_SIZE);
            TM_USART_ClearBuffer(USART6);
            TM_DISCO_LedToggle(LED_GREEN);
//            char tempInParse[50];
//            strcpy(tempInParse,note);
//            for(int i=0;i<50;i++)
//            {
//                tempInParse[i] = note[i];
//            }
            sprintf(sbuff, "Note:%s",note);
            LCD_StringLine(180, 300, (uint8_t *)sbuff);
            userNotes[index] = parsingLine(note);
            index++;
            if(index>10)
                index=0;
        }
        

    }
}

static void vDA2_ShowLCD(void *pvParameters)
{
    for(;;)
    {
        //vTaskDelay( 300 / portTICK_RATE_MS );
        mLCD_showSensor();
        mLCD_showDateTime();
    }
}
    
int mLCD_showSensor()
{
    LCD_CharSize(16);
    LCD_SetTextColor(RED);
    //LCD_Clear_P(WHITE, 110, 240, 5120);
    sprintf(sbuff, "%d*C , %02d lux , %02d humi   ",InRoomEvn.NhietDo,InRoomEvn.AnhSang,InRoomEvn.DoAmKhi);
    LCD_StringLine(110, 250, (uint8_t *)sbuff);
    
    sprintf(sbuff, "Distance : %0.2f",userData.distance);
    LCD_StringLine(150, 250, (uint8_t *)sbuff);
    
    //sprintf(sbuff, "Note:%s",note);
    //sprintf(sbuff,"%d:%d %d-%d-%d %s",userNotes[0].noteTime.hours, userNotes[0].noteTime.minutes ,userNotes[0].noteTime.day, userNotes[0].noteTime.month, userNotes[0].noteTime.year, userNotes[0].note);
    //LCD_StringLine(180, 250, (uint8_t *)sbuff);
    return 1;
}

int mLCD_showDateTime()
{
    LCD_SetTextColor(GREEN);
    LCD_CharSize(24);
    sprintf(sbuff, "%02d:%02d:%02d", time.hours, time.minutes, time.seconds);
    LCD_StringLine(62, 235, (uint8_t *)sbuff);
    if (time.hours == 0 && time.minutes == 00 && time.seconds == 0)
    {
        LCD_Clear_P(BLACK, 86, 320, 5120);
        LCD_CharSize(16);
        sprintf(sbuff, "%s,%d %s %d", date[time.day], time.date, Month[time.month],time.year + 2000);
        LCD_StringLine(86, 230, (uint8_t *)sbuff);
    }
    return 1;
}

int mLCD_resetLcd()
{

    TM_DS1307_GetDateTime(&time);
    
    LCD_SetTextColor(GREEN);
    LCD_CharSize(24);
    sprintf(sbuff, "%02d:%02d:%02d", time.hours, time.minutes, time.seconds);
    LCD_StringLine(62, 235, (uint8_t *)sbuff);
    
    LCD_Clear_P(BLACK, 86, 320, 5120);
    LCD_CharSize(16);
    sprintf(sbuff, "%s,%d,%s,%d", date[time.day], time.date, Month[time.month],time.year + 2000);
    LCD_StringLine(86, 250, (uint8_t *)sbuff);

    
    LCD_CharSize(16);
    LCD_SetTextColor(RED);
    sprintf(sbuff, "%d*C , %02d lux , %02d humi",InRoomEvn.NhietDo,InRoomEvn.AnhSang,InRoomEvn.DoAmKhi);
    LCD_StringLine(110, 250, (uint8_t *)sbuff);
    /*Reset color*/
    LCD_SetTextColor(WHITE);
    return 1;
}

int mLCD_showTitle()
{
    LCD_Clear_P(BLACK,14,320,8960);
    LCD_SetBackColor(BLACK);
    LCD_SetTextColor(BLUE);
    LCD_CharSize(16);
    LCD_StringLine(14,290,(unsigned char*)"UNIVERSITY OF INFOMATION TECHNOLOGY");
    LCD_StringLine(28,275,(unsigned char*)"FACULTY OF COMPUTER ENGINEERING");
    /*LCD_SetTextColor(RED);
    LCD_StringLine(42,180,(unsigned char*)"E_DESK");*/
    return 1;
}

float getDistance(uint16_t voltaValue)
{
    float volts = voltaValue*0.0048828125;
    // value from sensor * (5/1024) - if running 3.3.volts then change 5 to 3.3
    float distance = 65*pow(volts, -1.10);
    return distance;
}

//NOTE parsingLine(char* inputString)
//{
//    NOTE tempNoteType;
//    char * pch;
//    char tempNote[3][32] = {0};
//    char tempTime1[2][2] = {0};
//    char tempTime2[3][4] = {0};
//    pch = strtok (inputString," ");
//    if(pch!=NULL)
//    {
//        strcpy(tempNote[0],pch);
//        pch = strtok (NULL," ");
//        strcpy(tempNote[1],pch);
//        pch = strtok (NULL,".");
//        strcpy(tempNote[2],pch);
//    }
//    /*Parsing Time hour in tempNote[0]*/
//    /*  tempTime1[0] = (string)hour;
//    *   tempTime1[1] = (string)minutes;
//    */
//    pch = strtok(tempNote[0],":-'");
//    if(pch!=NULL)
//    {
//        strcpy(tempTime1[0],pch);
//        pch = strtok(NULL,":-'");
//        strcpy(tempTime1[1],pch);
//    }
//    tempNoteType.noteTime.hours = atoi(tempTime1[0]);
//    tempNoteType.noteTime.minutes = atoi(tempTime1[1]);
//    /*Parsing Day in tempNote[1]*/
//    /*  tempTime2[0] = (string)day;
//    *   tempTime2[1] = (string)month;
//    *   tempTime2[2] = (string)year;
//    */
//    pch = strtok(tempNote[1],":-'");
//    if(pch!=NULL)
//    {
//        strcpy(tempTime2[0],pch);
//        pch = strtok(NULL,":-'");
//        strcpy(tempTime2[1],pch);
//        pch = strtok(NULL,":-'");
//        strcpy(tempTime2[2],pch);
//    }
//    tempNoteType.noteTime.day = atoi(tempTime2[0]);
//    tempNoteType.noteTime.month = atoi(tempTime2[1]);
//    tempNoteType.noteTime.year = atoi(tempTime2[2]);
//    
//    /*Parsing text note in tempNote[2]*/
//    strcpy(tempNoteType.note,tempNote[2]);
//    
//    /*Return to NOTE*/
//    pch =NULL;
//    return tempNoteType;
//}

NOTE parsingLine(char* inputString)
{
    NOTE tempNoteType;
    char *pch;
    pch = strtok (inputString,":");
    if(pch!=NULL)
    {
        tempNoteType.noteTime.hours = atoi(pch);
        
        pch = strtok (NULL," ");
        tempNoteType.noteTime.minutes = atoi(pch);
        
        pch = strtok (NULL,"-");
        tempNoteType.noteTime.day = atoi(pch);
        
        pch = strtok (NULL,"-");
        tempNoteType.noteTime.month = atoi(pch);
        
        pch = strtok (NULL," ");
        tempNoteType.noteTime.year = atoi(pch);
        
        pch = strtok (NULL,".");
        strcpy(tempNoteType.note,pch);
        
        free(pch);
    }
    return tempNoteType;
}


#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line) {
  /* User can add his own implementation to report the file name and line
     number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

  /* Infinite loop */
  while (1) {
  }
}
#endif
