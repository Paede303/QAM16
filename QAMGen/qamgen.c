/*
 * qamgen.c
 *
 * Created: 05.05.2020 16:24:59
 *  Author: Chaos
 */ 
 #include "avr_compiler.h"
 #include "pmic_driver.h"
 #include "TC_driver.h"
 #include "clksys_driver.h"
 #include "sleepConfig.h"
 #include "port_driver.h"

 #include "FreeRTOS.h"
 #include "task.h"
 #include "queue.h"
 #include "event_groups.h"
 #include "semphr.h"
 #include "stack_macros.h"

 #include "mem_check.h"
#include "qamgen.h"

/* QAM settings. */
#define AMPLITUDE_1 0x01
#define AMPLITUDE_2 0x02
#define DATEN_AUFBEREITET 0x04

#define ein_kHz_0_25V 0x10
#define ein_kHz_0_50V 0x20
#define ein_kHZ_0_75V 0x40
#define ein_kHZ_1_00V 0x80
#define zwei_kHz_0_25V 0x100UL
#define zwei_kHz_0_50V 0x200UL
#define zwei_kHz_0_75V 0x400UL
#define zwei_kHz_1_00V 0x800UL

#define NR_OF_DATA_SAMPLES 32UL

/* DAC settings. */
#define NR_OF_GENERATOR_SAMPLES					32UL
#define GENERATOR_FREQUENCY_INITIAL_VALUE		1000UL

/* Flags to show QAM channel ready for new amplitude level. */
#define QAMCHANNEL_1_READY      0x01
#define QAMCHANNEL_2_READY      0x02

/* Defines which bits are representing the amount of data bytes to be sent. */
#define DATABYTETOSENDMASK      0x1F


typedef enum
{
    Idle,
    sendSyncByte,
    sendDatenbuffer,
    sendChecksum
} eProtokollStates;

typedef enum
{
	amp_025,
	amp_050,
	amp_075,
	amp_100
} ein_kHz_amplitude;

TaskHandle_t xsendFrame;

EventGroupHandle_t xQAMchannel_1;
EventGroupHandle_t xQAMchannel_2;

QueueHandle_t xQueue_Data;

/* Predefined sine arrays with 32 values. */
/*const int16_t sinLookup1000[NR_OF_GENERATOR_SAMPLES] = {   0,   80,  157,  228,  290,  341,  378,  402,
                                                         410,  402,  378,  341,  290,  228,  157,   80,
                                                           0,  -80, -157, -228, -290, -341, -378, -402, 
                                                        -410, -402, -378, -341, -290, -228, -157,  -80
                                                         };*/
														 
const int16_t sinLookup1k0_25V[NR_OF_GENERATOR_SAMPLES] = { 0,   61,  119,  172,  219,  258,  287,  304,
															310,  304,  287,  258,  219,  172,  119,   61,   
															0,  -61, -119, -172, -219, -258, -287, -304, 
															-310, -304, -287, -258, -219, -172, -119,  -61,
														  };
														  
const int16_t sinLookup1k0_5V[NR_OF_GENERATOR_SAMPLES] = { 0,  121,  237,  345,  439,  516,  573,  609,  
														   621,  609,  573,  516,  439,  345,  237,  121,    
														   0, -121, -237, -345, -439, -516, -573, -609, 
														   -621, -609, -573, -516, -439, -345, -237, -121,
														 };
														 
const int16_t sinLookup1k0_75V[NR_OF_GENERATOR_SAMPLES] = { 0,  182,  356,  517,  658,  774,  860,  913,  
															931,  913,  860,  774,  658,  517,  356,  182,    
															0, -182, -356, -517, -658, -774, -860, -913, 
															-931, -913, -860, -774, -658, -517, -356, -182,
														  };
														  
const int16_t sinLookup1k1V[NR_OF_GENERATOR_SAMPLES] = { 0,  242,  475,  690,  878, 1032, 1147, 1217, 1241, 
														 1217, 1147, 1032,  878,  690,  475,  242,    0, 
														 -242, -475, -690, -878, -1032, -1147, -1217, -1241, 
														 -1217, -1147, -1032, -878, -690, -475, -242,
														  };
														
                                                         
const int16_t sinLookup2k0_25V[NR_OF_GENERATOR_SAMPLES] = { 0,  119,  219,  287,  310,  287,  219,  119,    0, -119, -219, -287, -310, -287, -219, -119,    0,  119,  219,  287,  310,  287,  219,  119,    0, -119, -219, -287, -310, -287, -219, -119,
                                                         };
const int16_t sinLookup2k0_5V[NR_OF_GENERATOR_SAMPLES] = { 0,  237,  439,  573,  621,  573,  439,  237,    0, -237, -439, -573, -621, -573, -439, -237,    0,  237,  439,  573,  621,  573,  439,  237,    0, -237, -439, -573, -621, -573, -439, -237, 
};
const int16_t sinLookup2k0_75V[NR_OF_GENERATOR_SAMPLES] = { 0,  356,  658,  860,  931,  860,  658,  356,    0, -356, -658, -860, -931, -860, -658, -356,    0,  356,  658,  860,  931,  860,  658,  356,    0, -356, -658, -860, -931, -860, -658, -356,
};
const int16_t sinLookup2k1V[NR_OF_GENERATOR_SAMPLES] = { 0,  475,  878, 1147, 1241, 1147,  878,  475,    0, -475, -878, -1147, -1241, -1147, -878, -475,    0,  475,  878, 1147, 1241, 1147,  878,  475,    0, -475, -878, -1147, -1241, -1147, -878, -475, 
};
/* Buffers for DAC with DMA. */
static uint16_t dacBuffer0[NR_OF_GENERATOR_SAMPLES] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
														0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
														0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
														0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static uint16_t dacBuffer1[NR_OF_GENERATOR_SAMPLES] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
														0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
														0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
														0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
                                                        
static uint16_t dacBuffer2[NR_OF_GENERATOR_SAMPLES] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static uint16_t dacBuffer3[NR_OF_GENERATOR_SAMPLES] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

void initDAC(void) {
	DACB.CTRLA = DAC_CH0EN_bm | DAC_CH1EN_bm;
	DACB.CTRLB = DAC_CHSEL1_bm | DAC_CH0TRIG_bm | DAC_CH1TRIG_bm;
	DACB.CTRLC = DAC_REFSEL0_bm; // Reference Voltage = AVCC
	DACB.EVCTRL = 0x00;
	DACB.CTRLA |= DAC_ENABLE_bm;
	PORTB.DIRSET = 0x04;
    PORTB.DIRSET = 0x08;
}
    
void initGenDMA(void) {
	DMA.CTRL = 0;
	DMA.CTRL = DMA_RESET_bm;
	while ((DMA.CTRL & DMA_RESET_bm) != 0);

	DMA.CTRL = DMA_ENABLE_bm | DMA_DBUFMODE_CH01CH23_gc;

	DMA.CH0.REPCNT = 0;
	DMA.CH0.CTRLB|=0x01;
	DMA.CH0.CTRLA = DMA_CH_BURSTLEN_2BYTE_gc | DMA_CH_SINGLE_bm | DMA_CH_REPEAT_bm;					// ADC result is 2 byte 12 bit word
	DMA.CH0.ADDRCTRL =	DMA_CH_SRCRELOAD_TRANSACTION_gc | DMA_CH_SRCDIR_INC_gc | DMA_CH_DESTRELOAD_BURST_gc | DMA_CH_DESTDIR_INC_gc;	// reload source after every burst, reload dest after every transaction
	DMA.CH0.TRIGSRC = DMA_CH_TRIGSRC_DACB_CH0_gc;
	DMA.CH0.TRFCNT = NR_OF_GENERATOR_SAMPLES*2;	// always the number of bytes, even if burst length > 1
	DMA.CH0.SRCADDR0 = ((uint16_t)(&dacBuffer0[0]) >> 0) & 0xFF;
	DMA.CH0.SRCADDR1 = ((uint16_t)(&dacBuffer0[0]) >>  8) & 0xFF;
	DMA.CH0.SRCADDR2 =0;
	DMA.CH0.DESTADDR0 = ((uint16_t)(&DACB.CH0DATA) >> 0) & 0xFF;
	DMA.CH0.DESTADDR1 = ((uint16_t)(&DACB.CH0DATA) >> 8) & 0xFF;
	DMA.CH0.DESTADDR2 = 0;

	DMA.CH1.REPCNT = 0;
	DMA.CH1.CTRLB |= 0x01;
	DMA.CH1.CTRLA = DMA_CH_BURSTLEN_2BYTE_gc | DMA_CH_SINGLE_bm | DMA_CH_REPEAT_bm;
	DMA.CH1.ADDRCTRL = DMA_CH_SRCRELOAD_TRANSACTION_gc | DMA_CH_SRCDIR_INC_gc | DMA_CH_DESTRELOAD_BURST_gc | DMA_CH_DESTDIR_INC_gc;
	DMA.CH1.TRIGSRC = DMA_CH_TRIGSRC_DACB_CH0_gc;
	DMA.CH1.TRFCNT = NR_OF_GENERATOR_SAMPLES*2;
	DMA.CH1.SRCADDR0 = ((uint16_t)(&dacBuffer1[0]) >> 0) & 0xFF;
	DMA.CH1.SRCADDR1 = ((uint16_t)(&dacBuffer1[0]) >>  8) & 0xFF;
	DMA.CH1.SRCADDR2 =0;
	DMA.CH1.DESTADDR0 = ((uint16_t)(&DACB.CH0DATA) >> 0) & 0xFF;
	DMA.CH1.DESTADDR1 = ((uint16_t)(&DACB.CH0DATA) >> 8) & 0xFF;
	DMA.CH1.DESTADDR2 = 0;
    
    DMA.CH2.REPCNT = 0;
    DMA.CH2.CTRLB|=0x01;
    DMA.CH2.CTRLA = DMA_CH_BURSTLEN_2BYTE_gc | DMA_CH_SINGLE_bm | DMA_CH_REPEAT_bm;					// ADC result is 2 byte 12 bit word
    DMA.CH2.ADDRCTRL =	DMA_CH_SRCRELOAD_TRANSACTION_gc | DMA_CH_SRCDIR_INC_gc | DMA_CH_DESTRELOAD_BURST_gc | DMA_CH_DESTDIR_INC_gc;	// reload source after every burst, reload dest after every transaction
    DMA.CH2.TRIGSRC = DMA_CH_TRIGSRC_DACB_CH0_gc;
    DMA.CH2.TRFCNT = NR_OF_GENERATOR_SAMPLES*2;	// always the number of bytes, even if burst length > 1
    DMA.CH2.SRCADDR0 = ((uint16_t)(&dacBuffer2[0]) >> 0) & 0xFF;
    DMA.CH2.SRCADDR1 = ((uint16_t)(&dacBuffer2[0]) >>  8) & 0xFF;
    DMA.CH2.SRCADDR2 =0;
    DMA.CH2.DESTADDR0 = ((uint16_t)(&DACB.CH1DATA) >> 0) & 0xFF;
    DMA.CH2.DESTADDR1 = ((uint16_t)(&DACB.CH1DATA) >> 8) & 0xFF;
    DMA.CH2.DESTADDR2 = 0;
    
    DMA.CH3.REPCNT = 0;
    DMA.CH3.CTRLB |= 0x01;
    DMA.CH3.CTRLA = DMA_CH_BURSTLEN_2BYTE_gc | DMA_CH_SINGLE_bm | DMA_CH_REPEAT_bm;
    DMA.CH3.ADDRCTRL = DMA_CH_SRCRELOAD_TRANSACTION_gc | DMA_CH_SRCDIR_INC_gc | DMA_CH_DESTRELOAD_BURST_gc | DMA_CH_DESTDIR_INC_gc;
    DMA.CH3.TRIGSRC = DMA_CH_TRIGSRC_DACB_CH0_gc;
    DMA.CH3.TRFCNT = NR_OF_GENERATOR_SAMPLES*2;
    DMA.CH3.SRCADDR0 = ((uint16_t)(&dacBuffer3[0]) >> 0) & 0xFF;
    DMA.CH3.SRCADDR1 = ((uint16_t)(&dacBuffer3[0]) >>  8) & 0xFF;
    DMA.CH3.SRCADDR2 =0;
    DMA.CH3.DESTADDR0 = ((uint16_t)(&DACB.CH1DATA) >> 0) & 0xFF;
    DMA.CH3.DESTADDR1 = ((uint16_t)(&DACB.CH1DATA) >> 8) & 0xFF;
    DMA.CH3.DESTADDR2 = 0;
	
	DMA.CH0.CTRLA |= DMA_CH_ENABLE_bm;
	DMA.CH1.CTRLA |= DMA_CH_ENABLE_bm; 
    DMA.CH2.CTRLA |= DMA_CH_ENABLE_bm;
    DMA.CH3.CTRLA |= DMA_CH_ENABLE_bm;
}
void initDACTimer(void) {
	TC0_ConfigClockSource(&TCD0, TC_CLKSEL_DIV1_gc);
	TC0_ConfigWGM(&TCD0, TC_WGMODE_SINGLESLOPE_gc);
	TC_SetPeriod(&TCD0, 32000000/(GENERATOR_FREQUENCY_INITIAL_VALUE*NR_OF_GENERATOR_SAMPLES));
	EVSYS.CH0MUX = EVSYS_CHMUX_TCD0_OVF_gc; //Setup Eventsystem with timer TCD0 overflow
}

void vQuamGen(void *pvParameters) {
	initDAC();
	initDACTimer();
	initGenDMA();
    
       xQAMchannel_1=xEventGroupCreate();
       xQAMchannel_2=xEventGroupCreate();
       xQueue_Data = xQueueCreate( 1, sizeof(uint8_t)*NR_OF_DATA_SAMPLES);
    
        xTaskCreate(vsendFrame, NULL, configMINIMAL_STACK_SIZE+400, NULL, 2, &xsendFrame);
	
	for(;;) {
		vTaskDelay(1000/portTICK_RATE_MS);
	}
}

void fillBuffer(uint16_t buffer[NR_OF_GENERATOR_SAMPLES]) {
    
    uint8_t  EventGroupBits= xEventGroupGetBitsFromISR(xQAMchannel_1);
	/*ein_kHz_amplitude Amplitude = amp_100;
	
	if(EventGroupBits&ein_kHz_0_25V)
	{
		ein_kHz_amplitude Amplitude = amp_025;
	}
	if(EventGroupBits&ein_kHz_0_50V)
	{
		ein_kHz_amplitude Amplitude = amp_050;
	}
	if(EventGroupBits&ein_kHZ_0_75V)
	{
		ein_kHz_amplitude Amplitude = amp_075;
	}
	if(EventGroupBits&ein_kHZ_1_00V)
	{
		ein_kHz_amplitude Amplitude = amp_100;
	}*/
	
    switch(EventGroupBits)
	{
		case ein_kHz_0_25V:
		{
        		for(int i = 0; i < NR_OF_GENERATOR_SAMPLES;i++) {
	        		buffer[i] = 0x800+(sinLookup1k0_25V[i]); //0x800 war offset
        		};
		break;
		}

		case ein_kHz_0_50V:
		{
			for(int i = 0; i < NR_OF_GENERATOR_SAMPLES;i++) {
				buffer[i] = 0x800+(sinLookup1k0_5V[i]); //0x800 war offset
			};
		break;
		}

		case ein_kHZ_0_75V:
		{
			for(int i = 0; i < NR_OF_GENERATOR_SAMPLES;i++) {
				buffer[i] = 0x800+(sinLookup1k0_75V[i]); //0x800 war offset
			};		
		break;
		}

		case ein_kHZ_1_00V:
		{
			for(int i = 0; i < NR_OF_GENERATOR_SAMPLES;i++) {
				buffer[i] = 0x800+(sinLookup1k1V[i]); //0x800 war offset
			};
		break;
		}
	}
		
        xEventGroupSetBits(xQAMchannel_1,DATEN_AUFBEREITET);
}

// Mit ISR EventGroups arbeiten, da der Interrupt Buffer f??llt
void fillBuffer_1(uint16_t buffer[NR_OF_GENERATOR_SAMPLES]) {
    static uint8_t Amp_1=1;
    volatile uint16_t  EventGroupBits= xEventGroupGetBitsFromISR(xQAMchannel_2);
    Amp_1++;
   switch(EventGroupBits)
   {
	   case zwei_kHz_0_25V:
	   {
		   for(int i = 0; i < NR_OF_GENERATOR_SAMPLES;i++) {
			   buffer[i] = 0x800+(sinLookup2k0_25V[i]); //0x800 war offset
		   };
		   break;
	   }

	   case zwei_kHz_0_50V:
	   {
		   for(int i = 0; i < NR_OF_GENERATOR_SAMPLES;i++) {
			   buffer[i] = 0x800+(sinLookup2k0_5V[i]); //0x800 war offset
		   };
		   break;
	   }

	   case zwei_kHz_0_75V:
	   {
		   for(int i = 0; i < NR_OF_GENERATOR_SAMPLES;i++) {
			   buffer[i] = 0x800+(sinLookup2k0_75V[i]); //0x800 war offset
		   };
		   break;
	   }

	   case zwei_kHz_1_00V:
	   {
		   for(int i = 0; i < NR_OF_GENERATOR_SAMPLES;i++) {
			   buffer[i] = 0x800+(sinLookup2k1V[i]); //0x800 war offset
		   };
		   break;
	   }
   }
   
   xEventGroupSetBits(xQAMchannel_2,DATEN_AUFBEREITET);
   }

ISR(DMA_CH0_vect)
{
	DMA.CH0.CTRLB|=0x10;
	fillBuffer(&dacBuffer0[0]);
}

ISR(DMA_CH1_vect)
{
	DMA.CH1.CTRLB|=0x10;
	fillBuffer(&dacBuffer1[0]);
}

ISR(DMA_CH2_vect)
{
    
    DMA.CH2.CTRLB|=0x10;
    fillBuffer_1(&dacBuffer2[0]);
}

ISR(DMA_CH3_vect)
{
    DMA.CH3.CTRLB|=0x10;
    fillBuffer_1(&dacBuffer3[0]);
}

void vsendCommand(uint8_t Data[])
{

    if( xQueue_Data != 0 )
    {
        /* Put data to send to xQueue_Data. */
        xQueueSend(xQueue_Data,(void *)Data,pdMS_TO_TICKS(10));
    }    
}

void vsendFrame(void *pvParameters)
{
(void) pvParameters;
    
volatile uint8_t ucSendByteValue;                    // Variable current byte to send is stored
uint8_t ucSendBitPackageCounter = 0;        // Counts the sent bit packages, for QAM4 for 1 byte there are 4 packages (4 x 2bit = 8bit)
uint8_t ucReadyForNewDataByte = 1;          // Indicates if new data byte can be provided.
uint8_t ucNewDataByteValue = 0;             // Stores the new data byte value, which should be sent next.
uint8_t ucDataByteCounter = 0;              // Counts the sent data of the data array.
uint8_t ucDataBytesToSend;                  // Stores the value of how many data bytes should be sent. Extracted from command byte (1st array in data queue).
uint8_t ucQAMChannelsReady = 0;             // Status register to check if both QAM channels got the new amplitude levels.
uint8_t ucIdleSendByteFlag = 0;             // Byte for differentiation of idle bytes to be sent between 0xAF and 0x05.
uint8_t ucChecksumValue = 0;                // Used for checksum calculation.
uint8_t Data[NR_OF_DATA_SAMPLES + 1] = {};  // Data bytes received from queue.                

    
    eProtokollStates Protokoll = Idle;
    
    while(1)
    {
        
        /************************************************************************/
        /* State machine for setting up data.                                   */
        /************************************************************************/
        switch(Protokoll)
        {
            case Idle:
            {
                if (ucIdleSendByteFlag == 0)
                {
                    /* Send first idle byte. */
                    if (ucReadyForNewDataByte)
                    {
                        ucIdleSendByteFlag = 1;
                        ucNewDataByteValue = 0x00; //AF
                        ucReadyForNewDataByte = 0;
                    }
                }
                else
                {
                    /* Send second idle byte. */
                    if (ucReadyForNewDataByte)
                    {
                        ucIdleSendByteFlag = 0;
                        ucNewDataByteValue = 0x00; //05
                        ucReadyForNewDataByte = 0;
                        
                        
                        /* Check if new Data was received. */
                        if (xQueueReceive(xQueue_Data,Data, pdMS_TO_TICKS(0)) == pdTRUE)
                        {
                            ucDataBytesToSend = Data[0] & DATABYTETOSENDMASK;
                            Protokoll = sendSyncByte;
                        }                                
                    }
                }
                break;
            }
            
            case sendSyncByte:
            {
                if (ucReadyForNewDataByte)
                {
                    ucNewDataByteValue = 0xFF;
                    ucReadyForNewDataByte = 0;
                    Protokoll = sendDatenbuffer;
                }
                
                break;
            }                          
            
            case sendDatenbuffer:
            {  
                if (ucReadyForNewDataByte)
                {
                    if (ucDataByteCounter <= ucDataBytesToSend)
                    {
                        ucNewDataByteValue = Data[ucDataByteCounter];
                        
                        /* Continuous checksum calculation over all sent data bytes. */
                        ucChecksumValue ^= ucNewDataByteValue;
                        ucReadyForNewDataByte = 0;
                        ucDataByteCounter++;
                    } 
                    else
                    {
                        ucDataByteCounter = 0;
                        Protokoll = sendChecksum;
                    }
                }

                break;
            }
            case sendChecksum:
            {
                if (ucReadyForNewDataByte)
                {
                    ucNewDataByteValue = ucChecksumValue;
                    ucChecksumValue = 0;
                    ucReadyForNewDataByte = 0;
                    Protokoll = Idle;
                }
                break;
            }
            default:
            {
                Protokoll = Idle;
                break;
            }

        }
        
        
        /************************************************************************/
        /* Data send part.                                                      */
        /************************************************************************/
        
        /* Check if all data bit packages of one byte were sent. */
        if (ucSendBitPackageCounter > 1) //3
        {
            /* Then the new data byte can be loaded. */
            ucSendByteValue = ucNewDataByteValue;
            ucSendBitPackageCounter = 0;
            ucReadyForNewDataByte = 1;
        }
        
        /* Check if QAM channel 1 got amplitude value. */
        if (xEventGroupGetBits(xQAMchannel_1) & DATEN_AUFBEREITET)
        {
            /* Then flag can be deleted an status can be stored temporarily. */
            xEventGroupClearBits(xQAMchannel_1, DATEN_AUFBEREITET);
            ucQAMChannelsReady |= QAMCHANNEL_1_READY;
            
            /* New amplitude level for next transmission can be prepared. */
            switch (ucSendByteValue & 0b000000011)
            {
	            case 0b00000000:
	            xEventGroupSetBits(xQAMchannel_1, ein_kHz_0_25V);
	            xEventGroupClearBits(xQAMchannel_1, ein_kHz_0_50V|ein_kHZ_0_75V|ein_kHZ_1_00V);
	            break;
	            
	            case 0b00000001:
	            xEventGroupSetBits(xQAMchannel_1, ein_kHz_0_50V);
	            xEventGroupClearBits(xQAMchannel_1, ein_kHz_0_25V|ein_kHZ_0_75V|ein_kHZ_1_00V);
	            break;
	            
	            case 0b00000010:
	            xEventGroupSetBits(xQAMchannel_1, ein_kHZ_0_75V);
	            xEventGroupClearBits(xQAMchannel_1, ein_kHz_0_50V|ein_kHz_0_25V|ein_kHZ_1_00V);
	            break;
	            
	            case 0b00000011:
	            xEventGroupSetBits(xQAMchannel_1, ein_kHZ_1_00V);
	            xEventGroupClearBits(xQAMchannel_1, ein_kHz_0_50V|ein_kHZ_0_75V|ein_kHz_0_25V);
	            break;
            }        
		}
        
        /* Check if QAM channel 2 got amplitude value. */
        if (xEventGroupGetBits(xQAMchannel_2)&DATEN_AUFBEREITET)
        {
            /* Then flag can be deleted an status can be stored temporarily. */
            xEventGroupClearBits(xQAMchannel_2,DATEN_AUFBEREITET);
            ucQAMChannelsReady |= QAMCHANNEL_2_READY;
            
            /* New amplitude level for next transmission can be prepared. */
            switch (ucSendByteValue & 0b000001100)
            {
				case 0b00000000:
                xEventGroupSetBits(xQAMchannel_2, zwei_kHz_0_25V);
                xEventGroupClearBits(xQAMchannel_2, zwei_kHz_0_50V|zwei_kHz_0_75V|zwei_kHz_1_00V);
				break;
				
				case 0b00000100:
				xEventGroupSetBits(xQAMchannel_2, zwei_kHz_0_50V);
				xEventGroupClearBits(xQAMchannel_2, zwei_kHz_0_25V|zwei_kHz_0_75V|zwei_kHz_1_00V);
				break;
				
				case 0b00001000:
				xEventGroupSetBits(xQAMchannel_2, zwei_kHz_0_75V);
				xEventGroupClearBits(xQAMchannel_2, zwei_kHz_0_50V|zwei_kHz_0_25V|zwei_kHz_1_00V);
				break;
				
				case 0b00001100:
				xEventGroupSetBits(xQAMchannel_2, zwei_kHz_1_00V);
				xEventGroupClearBits(xQAMchannel_2, zwei_kHz_0_50V|zwei_kHz_0_75V|zwei_kHz_0_25V);
				break;
            }
        }        
        
        /* Check if both channels got the new amplitude level. */
        if((ucQAMChannelsReady & QAMCHANNEL_1_READY) && (ucQAMChannelsReady & QAMCHANNEL_2_READY))
        {
            /* Then new bit package can be prepared. */
            ucQAMChannelsReady = 0;
            ucSendBitPackageCounter++;
            ucSendByteValue = ucSendByteValue >> 4; //2
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}    