#ifndef MCU_ASSEMBLY_H
#define MCU_ASSEMBLY_H
#include "transport.h"
void assembly_start(void);
bool assembly_ready(void);
locker_status_t assembly_dispatch(void *,const mcu_request_context_t *,const locker_wire_request_union_t *);
void assembly_session_lost(void);
void assembly_transmit(mcu_transport_t *);
void assembly_progress(uint8_t task_id,uint64_t progress);
extern volatile uint32_t s205_diag[16];
#endif
