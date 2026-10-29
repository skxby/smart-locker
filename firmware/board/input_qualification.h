#ifndef LOCKER_INPUT_QUALIFICATION_H
#define LOCKER_INPUT_QUALIFICATION_H
/* PE0/PE1 low-active measured 2026-10-29: raw-open-absent.json and
   raw-closed-present.json in docs/evidence/stage-02/S2-02.
   Qualify each electrical mapping from measured raw levels, independently of
   the global (still UNVERIFIED) actuator profile. Never enable ADC before PC0
   wiring, protection and voltage/reference calibration have been checked. */
#define BOARD_DOOR_QUALIFIED 1
#define BOARD_DOOR_CLOSED_LEVEL 0
#define BOARD_ITEM_QUALIFIED 1
#define BOARD_ITEM_PRESENT_LEVEL 0
#ifndef BOARD_ADC_QUALIFIED
#define BOARD_ADC_QUALIFIED 0
#endif
#define BOARD_ADC_REFERENCE_MV 3300U
#define BOARD_ADC_MIN_CODE 1U
#define BOARD_ADC_MAX_CODE 4094U
#endif
