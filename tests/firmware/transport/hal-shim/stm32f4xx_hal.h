#ifndef S203_TEST_HAL_H
#define S203_TEST_HAL_H
#include <stdint.h>
extern uint8_t fixture_uid[12];
#define UID_BASE fixture_uid
typedef int HAL_StatusTypeDef;
#define HAL_OK 0
#define HAL_ERROR 1
typedef struct { unsigned Channel, Direction, PeriphInc, MemInc, PeriphDataAlignment, MemDataAlignment, Mode, Priority, FIFOMode; } DMA_InitTypeDef;
typedef struct { void *Instance; DMA_InitTypeDef Init; } DMA_HandleTypeDef;
typedef struct { unsigned BaudRate, WordLength, StopBits, Parity, Mode, HwFlowCtl, OverSampling; } UART_InitTypeDef;
typedef struct { void *Instance; UART_InitTypeDef Init; DMA_HandleTypeDef *hdmarx, *hdmatx; } UART_HandleTypeDef;
typedef struct { unsigned Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;
#define GPIOD ((void *)4)
#define USART2 ((void *)2)
#define DMA1_Stream5 ((void *)5)
#define DMA1_Stream6 ((void *)6)
#define USART2_IRQn 2
#define DMA1_Stream5_IRQn 5
#define DMA1_Stream6_IRQn 6
#define GPIO_PIN_5 32u
#define GPIO_PIN_6 64u
#define GPIO_MODE_AF_PP 2u
#define GPIO_PULLUP 1u
#define GPIO_SPEED_FREQ_VERY_HIGH 3u
#define GPIO_AF7_USART2 7u
#define DMA_CHANNEL_4 4u
#define DMA_PERIPH_TO_MEMORY 0u
#define DMA_MEMORY_TO_PERIPH 1u
#define DMA_PINC_DISABLE 0u
#define DMA_MINC_ENABLE 1u
#define DMA_PDATAALIGN_BYTE 0u
#define DMA_MDATAALIGN_BYTE 0u
#define DMA_CIRCULAR 1u
#define DMA_NORMAL 0u
#define DMA_PRIORITY_HIGH 2u
#define DMA_FIFOMODE_DISABLE 0u
#define UART_WORDLENGTH_8B 8u
#define UART_STOPBITS_1 1u
#define UART_PARITY_NONE 0u
#define UART_MODE_TX_RX 3u
#define UART_HWCONTROL_NONE 0u
#define UART_OVERSAMPLING_16 16u
void fixture_clock(void);
uint16_t fixture_counter(DMA_HandleTypeDef *);
#define __HAL_RCC_GPIOD_CLK_ENABLE() fixture_clock()
#define __HAL_RCC_DMA1_CLK_ENABLE() fixture_clock()
#define __HAL_RCC_USART2_CLK_ENABLE() fixture_clock()
#define __HAL_DMA_GET_COUNTER(dma) fixture_counter(dma)
#define __HAL_LINKDMA(h,field,dma) ((h)->field = &(dma))
#define __DMB() ((void)0)
void HAL_GPIO_Init(void *, GPIO_InitTypeDef *);
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *);
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *);
void HAL_NVIC_SetPriority(int, unsigned, unsigned);
void HAL_NVIC_EnableIRQ(int);
void HAL_UART_IRQHandler(UART_HandleTypeDef *);
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *);
#endif
