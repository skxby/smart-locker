/* S2-02 read-only peripherals. No lock GPIO/timer, UART, Flash write or ledger. */
#include <string.h>
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "peripherals.h"
#include "input_qualification.h"
#include "timebase.h"
#include "inputs.h"
#include "rc522.h"

static SPI_HandleTypeDef spi[2];
static ADC_HandleTypeDef adc;
static SemaphoreHandle_t bus_mutex[2];
static bool peripherals_ready;
static locker_board_inputs_t published;
static const input_mapping_t mapping = {
    BOARD_DOOR_QUALIFIED, BOARD_ITEM_QUALIFIED, BOARD_ADC_QUALIFIED,
    BOARD_DOOR_CLOSED_LEVEL, BOARD_ITEM_PRESENT_LEVEL,
    BOARD_ADC_REFERENCE_MV, 100000U, 22000U,
    BOARD_ADC_MIN_CODE, BOARD_ADC_MAX_CODE, 30U
};
volatile uint32_t s202_diag[40];
/* Index 0 is a publication sequence, odd while updating. See drivers/README. */
static void diag_begin(void) { taskENTER_CRITICAL(); s202_diag[0]++; }
static void diag_end(void) { s202_diag[0]++; taskEXIT_CRITICAL(); }

static void spi_pins(void)
{
    GPIO_InitTypeDef pin = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    /* Preload inactive CS levels before changing pin direction. */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, GPIO_PIN_SET);
    pin.Mode = GPIO_MODE_OUTPUT_PP; pin.Pull = GPIO_NOPULL; pin.Speed = GPIO_SPEED_FREQ_LOW;
    pin.Pin = GPIO_PIN_4; HAL_GPIO_Init(GPIOA, &pin);
    pin.Pin = GPIO_PIN_12; HAL_GPIO_Init(GPIOB, &pin);
    pin.Pin = GPIO_PIN_3; HAL_GPIO_Init(GPIOE, &pin);
    pin.Mode = GPIO_MODE_AF_PP; pin.Speed = GPIO_SPEED_FREQ_MEDIUM;
    pin.Alternate = GPIO_AF5_SPI1; pin.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOA, &pin);
    pin.Alternate = GPIO_AF5_SPI2; pin.Pin = GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOB, &pin);
    /* Known low-active dry contacts: bias the open circuit high even when
       the external pull-up connection is poor. This does not diagnose a
       broken two-wire contact; open wire still means non-closed/non-present. */
    pin.Mode = GPIO_MODE_INPUT; pin.Pull = GPIO_PULLUP;
    pin.Pin = GPIO_PIN_0 | GPIO_PIN_1; HAL_GPIO_Init(GPIOE, &pin);
}
locker_status_t board_peripherals_init(void)
{
    uint8_t i;
    if (peripherals_ready) return LOCKER_STATUS_OK;
    spi_pins();
    __HAL_RCC_SPI1_CLK_ENABLE(); __HAL_RCC_SPI2_CLK_ENABLE();
    for (i = 0U; i < 2U; ++i) {
        bus_mutex[i] = xSemaphoreCreateMutex();
        if (bus_mutex[i] == NULL) return LOCKER_STATUS_NO_SPACE;
        spi[i].Instance = i == 0U ? SPI1 : SPI2;
        spi[i].Init.Mode = SPI_MODE_MASTER;
        spi[i].Init.Direction = SPI_DIRECTION_2LINES;
        spi[i].Init.DataSize = SPI_DATASIZE_8BIT;
        spi[i].Init.CLKPolarity = SPI_POLARITY_LOW;
        spi[i].Init.CLKPhase = SPI_PHASE_1EDGE;
        spi[i].Init.NSS = SPI_NSS_SOFT;
        spi[i].Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;
        spi[i].Init.FirstBit = SPI_FIRSTBIT_MSB;
        spi[i].Init.TIMode = SPI_TIMODE_DISABLE;
        spi[i].Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
        if (HAL_SPI_Init(&spi[i]) != HAL_OK) return LOCKER_STATUS_IO_ERROR;
    }
#if BOARD_ADC_QUALIFIED
    {
        GPIO_InitTypeDef pin = {0};
        ADC_ChannelConfTypeDef channel = {0};
        __HAL_RCC_GPIOC_CLK_ENABLE(); __HAL_RCC_ADC1_CLK_ENABLE();
        pin.Pin = GPIO_PIN_0; pin.Mode = GPIO_MODE_ANALOG; pin.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOC, &pin);
        adc.Instance = ADC1;
        adc.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
        adc.Init.Resolution = ADC_RESOLUTION_12B;
        adc.Init.ScanConvMode = DISABLE; adc.Init.ContinuousConvMode = DISABLE;
        adc.Init.DiscontinuousConvMode = DISABLE;
        adc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
        adc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
        adc.Init.DataAlign = ADC_DATAALIGN_RIGHT; adc.Init.NbrOfConversion = 1;
        adc.Init.DMAContinuousRequests = DISABLE; adc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
        channel.Channel = ADC_CHANNEL_10; channel.Rank = 1;
        channel.SamplingTime = ADC_SAMPLETIME_480CYCLES;
        if (HAL_ADC_Init(&adc) != HAL_OK || HAL_ADC_ConfigChannel(&adc, &channel) != HAL_OK)
            return LOCKER_STATUS_IO_ERROR;
    }
#else
    (void)adc;
#endif
    peripherals_ready = true;
    return LOCKER_STATUS_OK;
}

locker_status_t mcu_board_spi_transfer(uint8_t cs, const uint8_t *tx,
                                      uint8_t *rx, uint16_t length, uint32_t timeout_us)
{
    static uint8_t dummy_tx[2][512], dummy_rx[2][512];
    HAL_StatusTypeDef hal_status;
    uint32_t start, elapsed;
    if (cs > BOARD_SPI_RC522 || length == 0U || length > 512U ||
        timeout_us < 1000U || timeout_us > 50000U || (tx == NULL && rx == NULL))
        return LOCKER_STATUS_INVALID;
    if (!peripherals_ready || __get_IPSR() != 0U || xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)
        return LOCKER_STATUS_NOT_READY;
    /* Nonblocking acquisition: never hold one bus while waiting for another. */
    if (xSemaphoreTake(bus_mutex[cs], 0U) != pdTRUE) {
        diag_begin(); s202_diag[28]++; diag_end();
        return LOCKER_STATUS_BUSY;
    }
    start = HAL_GetTick();
    HAL_GPIO_WritePin(cs == BOARD_SPI_FLASH ? GPIOA : GPIOB,
                     cs == BOARD_SPI_FLASH ? GPIO_PIN_4 : GPIO_PIN_12, GPIO_PIN_RESET);
    hal_status = HAL_SPI_TransmitReceive(&spi[cs], (uint8_t *)(tx == NULL ? dummy_tx[cs] : tx),
                rx == NULL ? dummy_rx[cs] : rx, length, timeout_us / 1000U);
    HAL_GPIO_WritePin(cs == BOARD_SPI_FLASH ? GPIOA : GPIOB,
                     cs == BOARD_SPI_FLASH ? GPIO_PIN_4 : GPIO_PIN_12, GPIO_PIN_SET);
    elapsed = HAL_GetTick() - start;
    xSemaphoreGive(bus_mutex[cs]);
    diag_begin();
    s202_diag[26U + cs]++;
    if (hal_status != HAL_OK) s202_diag[29]++;
    if (elapsed > s202_diag[34U + cs]) s202_diag[34U + cs] = elapsed;
    diag_end();
    return hal_status == HAL_OK ? LOCKER_STATUS_OK : LOCKER_STATUS_IO_ERROR;
}
locker_status_t board_inputs_copy(locker_board_inputs_t *out)
{
    uint64_t now;
    if (out == NULL) return LOCKER_STATUS_INVALID;
    taskENTER_CRITICAL(); *out = published; taskEXIT_CRITICAL();
    now = mcu_board_monotonic_ms();
    if (!peripherals_ready || now < out->observed_mono_ms || now - out->observed_mono_ms > 100U) {
        memset(out, 0, sizeof(*out));
        return LOCKER_STATUS_NOT_READY;
    }
    return LOCKER_STATUS_OK;
}
void board_input_task(void *argument)
{
    input_debounce_t door = {0}, item = {0};
    (void)argument;
    for (;;) {
        input_raw_t raw = {0};
        locker_board_inputs_t normalized;
        raw.door_level = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_0) == GPIO_PIN_SET;
        raw.item_level = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_1) == GPIO_PIN_SET;
        raw.gpio_valid = peripherals_ready;
#if BOARD_ADC_QUALIFIED
        if (HAL_ADC_Start(&adc) == HAL_OK) {
            if (HAL_ADC_PollForConversion(&adc, 2U) == HAL_OK) {
                raw.adc_code = (uint16_t)HAL_ADC_GetValue(&adc);
                raw.adc_valid = true;
            }
            HAL_ADC_Stop(&adc);
        }
#endif
        raw.mono_ms = mcu_board_monotonic_ms();
        input_normalize(&mapping, &raw, &normalized);
        diag_begin();
        published = normalized; /* Raw mapped facts; Control owns business debounce. */
        s202_diag[1] = raw.door_level; s202_diag[2] = raw.item_level;
        s202_diag[3] = normalized.door; s202_diag[4] = normalized.item;
        s202_diag[5] = normalized.supply_valid; s202_diag[6] = raw.adc_code;
        s202_diag[7] = normalized.supply_mv;
        s202_diag[8] = (uint32_t)raw.mono_ms; s202_diag[9] = (uint32_t)(raw.mono_ms >> 32);
        s202_diag[32] = input_debounce(&door, normalized.door, mapping.debounce_ms, raw.mono_ms);
        s202_diag[33] = input_debounce(&item, normalized.item, mapping.debounce_ms, raw.mono_ms);
        diag_end();
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}
static locker_status_t rfid_transfer(void *context, const uint8_t *tx, uint8_t *rx, uint16_t length)
{
    (void)context;
    return mcu_board_spi_transfer(BOARD_SPI_RC522, tx, rx, length, 5000U);
}
static uint64_t rfid_now(void *context) { (void)context; return mcu_board_monotonic_ms(); }
static void rfid_wait(void *context, uint32_t ms) { (void)context; vTaskDelay(pdMS_TO_TICKS(ms)); }
void board_storage_task(void *argument)
{
    rc522_t reader = {0};
    rc522_uid_t uid;
    uint8_t jedec_tx[4] = {0x9fU,0U,0U,0U}, rx[4] = {0};
    uint8_t status_tx[2] = {0x05U,0U};
    locker_status_t status;
    uint32_t i;
    (void)argument;
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, GPIO_PIN_RESET);
    vTaskDelay(pdMS_TO_TICKS(2U));
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(50U));
    reader.transfer = rfid_transfer; reader.now_ms = rfid_now; reader.wait_ms = rfid_wait;
    status = rc522_init(&reader);
    diag_begin(); s202_diag[10] = status; s202_diag[11] = reader.version; diag_end();
    status = mcu_board_spi_transfer(BOARD_SPI_FLASH, jedec_tx, rx, 4U, 5000U);
    diag_begin();
    s202_diag[24] = status == LOCKER_STATUS_OK ? ((uint32_t)rx[1] << 16) | ((uint32_t)rx[2] << 8) | rx[3] : 0U;
    diag_end();
    for (;;) {
        uint64_t start = mcu_board_monotonic_ms();
        status = rc522_read_uid(&reader, &uid);
        diag_begin();
        s202_diag[12] = status; s202_diag[13] = uid.length;
        s202_diag[37] = reader.last_irq; s202_diag[38] = reader.last_error;
        s202_diag[39] = ((uint32_t)reader.last_count << 16) | ((uint32_t)reader.last_bits << 8) | reader.last_command;
        for (i = 0U; i < 10U; ++i) s202_diag[14U + i] = uid.bytes[i];
        if (status == LOCKER_STATUS_OK) s202_diag[36]++;
        if (mcu_board_monotonic_ms() - start > s202_diag[30])
            s202_diag[30] = (uint32_t)(mcu_board_monotonic_ms() - start);
        diag_end();
        /* Only READ STATUS; CS is released by each transfer. No wait-busy
           loop, write-enable, erase, program or filesystem in this card. */
        status = mcu_board_spi_transfer(BOARD_SPI_FLASH, status_tx, rx, 2U, 5000U);
        diag_begin(); s202_diag[25] = status == LOCKER_STATUS_OK ? rx[1] : 0xffffffffU; diag_end();
        vTaskDelay(pdMS_TO_TICKS(100U));
    }
}
