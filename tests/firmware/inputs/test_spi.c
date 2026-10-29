#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdatomic.h>
#include <string.h>
#include <sched.h>
#include <time.h>
#include "fixture.h"
#include "peripherals.h"

GPIO_TypeDef fixture_gpio[4] = {{0},{1},{2},{3}};
pthread_mutex_t fixture_critical = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t buses[2] = {PTHREAD_MUTEX_INITIALIZER,PTHREAD_MUTEX_INITIALIZER};
static unsigned created;
static atomic_uint active[2], cs[2], isr, fail_next, accepted[2];
SemaphoreHandle_t xSemaphoreCreateMutex(void) { assert(created < 2); return &buses[created++]; }
int xSemaphoreTake(SemaphoreHandle_t lock, unsigned ticks) { assert(ticks == 0); return pthread_mutex_trylock(lock) == 0; }
int xSemaphoreGive(SemaphoreHandle_t lock) { return pthread_mutex_unlock(lock) == 0; }
int xTaskGetSchedulerState(void) { return taskSCHEDULER_RUNNING; }
void vTaskDelay(unsigned ms) { struct timespec t={ms/1000,(long)(ms%1000)*1000000}; nanosleep(&t,NULL); }
uint32_t __get_IPSR(void) { return atomic_load(&isr); }
uint64_t mcu_board_monotonic_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
uint32_t HAL_GetTick(void) { return (uint32_t)mcu_board_monotonic_ms(); }
void HAL_GPIO_Init(GPIO_TypeDef *gpio, GPIO_InitTypeDef *pin)
{
    if (gpio == GPIOE && pin->Pin == (GPIO_PIN_0 | GPIO_PIN_1))
        assert(pin->Mode == GPIO_MODE_INPUT && pin->Pull == GPIO_PULLUP);
}
void HAL_GPIO_WritePin(GPIO_TypeDef *gpio, uint32_t pin, unsigned value)
{
    if (gpio == GPIOA && pin == GPIO_PIN_4) atomic_store(&cs[0],value);
    if (gpio == GPIOB && pin == GPIO_PIN_12) atomic_store(&cs[1],value);
}
unsigned HAL_GPIO_ReadPin(GPIO_TypeDef *gpio, uint32_t pin) { (void)gpio; (void)pin; return 1; }
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *spi) { (void)spi; return HAL_OK; }
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *spi, uint8_t *tx, uint8_t *rx, uint16_t length, uint32_t timeout)
{
    unsigned bus = spi->Instance == SPI1 ? 0 : 1;
    assert(atomic_fetch_add(&active[bus],1) == 0);
    assert(atomic_load(&cs[bus]) == 0 && length <= 512 && timeout > 0);
    vTaskDelay(1);
    memcpy(rx,tx,length);
    assert(atomic_fetch_sub(&active[bus],1) == 1);
    return atomic_exchange(&fail_next,0) ? HAL_TIMEOUT : HAL_OK;
}
static void *worker(void *arg)
{
    uint8_t bus = *(uint8_t *)arg, tx[512], rx[512];
    unsigned i, completed = 0;
    memset(tx,bus+17,sizeof(tx));
    for (i=0; i<100000 && completed<30; ++i) {
        locker_status_t status=mcu_board_spi_transfer(bus,tx,rx,sizeof(tx),5000);
        if (status == LOCKER_STATUS_BUSY) { vTaskDelay(1); continue; }
        assert(status == LOCKER_STATUS_OK && memcmp(tx,rx,sizeof(tx)) == 0);
        ++completed; atomic_fetch_add(&accepted[bus],1);
    }
    assert(completed == 30);
    return NULL;
}
int main(void)
{
    uint8_t tx[2]={0x9f,0}, rx[2], bus[4]={0,0,1,1};
    pthread_t threads[4];
    locker_board_inputs_t inputs;
    unsigned i;
    assert(mcu_board_spi_transfer(0,tx,rx,2,5000) == LOCKER_STATUS_NOT_READY);
    assert(board_peripherals_init() == LOCKER_STATUS_OK);
    assert(board_peripherals_init() == LOCKER_STATUS_OK && created == 2);
    assert(board_inputs_copy(&inputs) == LOCKER_STATUS_NOT_READY && !inputs.supply_valid);
    assert(mcu_board_spi_transfer(2,tx,rx,2,5000) == LOCKER_STATUS_INVALID);
    assert(mcu_board_spi_transfer(0,tx,rx,513,5000) == LOCKER_STATUS_INVALID);
    assert(mcu_board_spi_transfer(0,tx,rx,2,999) == LOCKER_STATUS_INVALID);
    assert(mcu_board_spi_transfer(0,NULL,NULL,2,5000) == LOCKER_STATUS_INVALID);
    atomic_store(&isr,1);
    assert(mcu_board_spi_transfer(0,tx,rx,2,5000) == LOCKER_STATUS_NOT_READY);
    atomic_store(&isr,0);
    atomic_store(&fail_next,1);
    assert(mcu_board_spi_transfer(0,tx,rx,2,5000) == LOCKER_STATUS_IO_ERROR);
    assert(atomic_load(&cs[0]) == 1); /* Error releases CS and mutex. */
    assert(mcu_board_spi_transfer(0,NULL,rx,2,5000) == LOCKER_STATUS_OK && rx[0] == 0);
    assert(mcu_board_spi_transfer(0,tx,NULL,2,5000) == LOCKER_STATUS_OK);
    for (i=0;i<4;++i) assert(pthread_create(&threads[i],NULL,worker,&bus[i]) == 0);
    for (i=0;i<4;++i) assert(pthread_join(threads[i],NULL) == 0);
    assert(atomic_load(&accepted[0]) == 60 && atomic_load(&accepted[1]) == 60);
    assert(atomic_load(&cs[0]) == 1 && atomic_load(&cs[1]) == 1);
    assert(s202_diag[29] == 1 && s202_diag[28] > 0);
    return 0;
}
