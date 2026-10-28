/* Generated design declarations; do not edit. No product implementation. */
#ifndef LOCKER_CONTRACTS_H
#define LOCKER_CONTRACTS_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define LOCKER_CONTRACT_VERSION "0.2.1"
#define LOCKER_UART_BAUD 115200u
#define LOCKER_UART_MAX_PAYLOAD 512u
#define LOCKER_UART_HEADER_BYTES 10u
#define LOCKER_UART_EVENT_BYTES 96u
#define LOCKER_UART_MAX_EVENTS 4u
#define LOCKER_UART_DMA_BYTES 2048u
#define LOCKER_CORE_QUEUE_CAPACITY 64u
#define LOCKER_WS_QUEUE_CAPACITY 16u
#define LOCKER_HTTP_BODY_MAX 4096u
#define LOCKER_LOCAL_PACKET_MAX 65535u
#define LOCKER_IDEMPOTENCY_DAYS 30u
#define LOCKER_SESSION_HOURS 8u
#define LOCKER_PRESENCE_TTL_MS 15000u
#define LOCKER_PHYSICAL_FRESH_MS 2000u
#define LOCKER_PREPARE_DEADLINE_MS 2000u
#define LOCKER_REQUEST_TTL_MS 120000u
#define LOCKER_WAIT_OPEN_MS 30000u
#define LOCKER_WAIT_CLOSE_MS 120000u
#define LOCKER_WAIT_CONFIRM_MS 120000u
#define LOCKER_HOST_TIMEOUT_MS 5000u
#define LOCKER_HOST_BOOT_GRACE_MS 120000u
#define LOCKER_UI_BOOT_GRACE_MS 30000u
#define LOCKER_UI_RESTART_WINDOW_MS 600000u
#define LOCKER_UI_RESTART_LIMIT 3u
#define LOCKER_UI_RESTART_COOLDOWN_MS 60000u
#define LOCKER_LOCK_PULSE_MAX_MS 1000u
#define LOCKER_CORE_RESULT_WAIT_MS 1000u
#define LOCKER_CARD_OBSERVATION_MS 30000u
#define LOCKER_MAX_HOST_RESULT_BYTES 65535u
#define LOCKER_TIME_ANCHOR_PERIOD_MS 2000u
#define LOCKER_TIME_ANCHOR_MAX_RTT_MS 200u
#define LOCKER_TIME_ANCHOR_MAX_AGE_MS 3000u
#define LOCKER_CORE_COMPLETION_TOKEN_BYTES 16u
#define LOCKER_COMPLETION_RETENTION_MS 60000u
#define LOCKER_MAX_USERS 100u
#define LOCKER_LOCAL_REQUEST_MAX 8192u
#define LOCKER_VISION_REQUEST_MAX 8192u
#define LOCKER_VISION_RESPONSE_MAX 16384u
#define LOCKER_VISION_QUEUE_CAPACITY 4u
#define LOCKER_VISION_FRAME_COUNT 3u
#define LOCKER_VISION_CAPTURE_DEADLINE_MS 3000u
#define LOCKER_VISION_RESULT_MAX_AGE_MS 2000u
#define LOCKER_VISION_STATUS_PERIOD_MS 1000u
#define LOCKER_VISION_STATUS_TTL_MS 3000u
#define LOCKER_VISION_CONTROL_TIMEOUT_MS 1000u
typedef struct { uint8_t bytes[16]; } locker_uuid_t;
typedef struct { char device_id[25]; locker_uuid_t journal_epoch; uint64_t boot_id; uint64_t event_seq; } locker_event_key_t;
typedef enum { LOCKER_STATUS_OK=0, LOCKER_STATUS_ACCEPTED=1, LOCKER_STATUS_QUEUED=2, LOCKER_STATUS_INVALID=3, LOCKER_STATUS_BUSY=4, LOCKER_STATUS_NOT_READY=5, LOCKER_STATUS_CONFLICT=6, LOCKER_STATUS_UNKNOWN=7, LOCKER_STATUS_IO_ERROR=8, LOCKER_STATUS_NO_SPACE=9, LOCKER_STATUS_REPLAY_BLOCKED=10 } locker_status_t;
typedef enum { LOCKER_ROLE_USER=0, LOCKER_ROLE_ADMIN=1 } locker_role_t;
typedef enum { LOCKER_OPERATION_KIND_BORROW=1, LOCKER_OPERATION_KIND_RETURN=2 } locker_operation_kind_t;
typedef enum { LOCKER_OPERATION_STATE_REQUESTED=0, LOCKER_OPERATION_STATE_ACCESS_PENDING=1, LOCKER_OPERATION_STATE_ACCESS_ACTIVE=2, LOCKER_OPERATION_STATE_WAIT_CONFIRM=3, LOCKER_OPERATION_STATE_REVIEW_REQUIRED=4, LOCKER_OPERATION_STATE_COMPLETED=5, LOCKER_OPERATION_STATE_CANCELLED=6, LOCKER_OPERATION_STATE_NO_CHANGE=7, LOCKER_OPERATION_STATE_FAILED=8 } locker_operation_state_t;
typedef enum { LOCKER_DOOR_STATE_UNKNOWN=0, LOCKER_DOOR_STATE_CLOSED=1, LOCKER_DOOR_STATE_OPEN=2 } locker_door_state_t;
typedef enum { LOCKER_ITEM_STATE_UNKNOWN=0, LOCKER_ITEM_STATE_ABSENT=1, LOCKER_ITEM_STATE_PRESENT=2 } locker_item_state_t;
typedef enum { LOCKER_DEVICE_MODE_OFFLINE=0, LOCKER_DEVICE_MODE_RECONCILING=1, LOCKER_DEVICE_MODE_READY=2, LOCKER_DEVICE_MODE_DEGRADED=3 } locker_device_mode_t;
typedef enum { LOCKER_TOOL_AVAILABILITY_AVAILABLE=0, LOCKER_TOOL_AVAILABILITY_RESERVED=1, LOCKER_TOOL_AVAILABILITY_BORROWED=2, LOCKER_TOOL_AVAILABILITY_REVIEW=3, LOCKER_TOOL_AVAILABILITY_UNAVAILABLE=4 } locker_tool_availability_t;
typedef enum { LOCKER_TIME_QUALITY_UNSYNCED=0, LOCKER_TIME_QUALITY_ESTIMATED=1, LOCKER_TIME_QUALITY_HOST_SYNCED=2 } locker_time_quality_t;
typedef enum { LOCKER_RESOLUTION_COMPLETE_BORROW=0, LOCKER_RESOLUTION_COMPLETE_RETURN=1, LOCKER_RESOLUTION_NO_CHANGE=2 } locker_resolution_t;
typedef enum { LOCKER_COMMAND_STATE_NONE=0, LOCKER_COMMAND_STATE_ACCEPTED=1, LOCKER_COMMAND_STATE_PULSE_STARTED=2, LOCKER_COMMAND_STATE_PULSE_FINISHED=3, LOCKER_COMMAND_STATE_REJECTED=4, LOCKER_COMMAND_STATE_UNKNOWN_RESULT=5 } locker_command_state_t;
typedef enum { LOCKER_ALLOWED_ACTION_PRESENT_AT_CABINET=0, LOCKER_ALLOWED_ACTION_WAIT=1, LOCKER_ALLOWED_ACTION_CONFIRM=2, LOCKER_ALLOWED_ACTION_CANCEL=3, LOCKER_ALLOWED_ACTION_ADMIN_REVIEW=4 } locker_allowed_action_t;
typedef enum { LOCKER_ALARM_PATTERN_OFF=0, LOCKER_ALARM_PATTERN_NOTICE=1, LOCKER_ALARM_PATTERN_FAULT=2 } locker_alarm_pattern_t;
typedef enum { LOCKER_PROGRESS_TARGET_SERVICE=0, LOCKER_PROGRESS_TARGET_UI=1 } locker_progress_target_t;
typedef enum { LOCKER_MCU_TASK_ID_CONTROL=0, LOCKER_MCU_TASK_ID_COMMUNICATION=1, LOCKER_MCU_TASK_ID_PERIPHERAL_STORAGE=2, LOCKER_MCU_TASK_ID_HEALTH=3 } locker_mcu_task_id_t;
typedef enum { LOCKER_SUPERVISOR_STATE_UNKNOWN=0, LOCKER_SUPERVISOR_STATE_RUNNING=1, LOCKER_SUPERVISOR_STATE_UI_RECOVERING=2, LOCKER_SUPERVISOR_STATE_LIMITED=3 } locker_supervisor_state_t;
enum { LOCKER_WIRE_OPERATION_KIND_BORROW=1, LOCKER_WIRE_OPERATION_KIND_RETURN=2 };
enum { LOCKER_WIRE_COMMAND_STATE_NONE=0, LOCKER_WIRE_COMMAND_STATE_ACCEPTED=1, LOCKER_WIRE_COMMAND_STATE_PULSE_STARTED=2, LOCKER_WIRE_COMMAND_STATE_PULSE_FINISHED=3, LOCKER_WIRE_COMMAND_STATE_REJECTED=4, LOCKER_WIRE_COMMAND_STATE_UNKNOWN_RESULT=5 };
enum { LOCKER_WIRE_DEVICE_EVENT_TYPE_BOOT=1, LOCKER_WIRE_DEVICE_EVENT_TYPE_PULSE_STARTED=2, LOCKER_WIRE_DEVICE_EVENT_TYPE_PULSE_FINISHED=3, LOCKER_WIRE_DEVICE_EVENT_TYPE_DOOR_OPENED=4, LOCKER_WIRE_DEVICE_EVENT_TYPE_DOOR_CLOSED=5, LOCKER_WIRE_DEVICE_EVENT_TYPE_ITEM_REMOVED=6, LOCKER_WIRE_DEVICE_EVENT_TYPE_ITEM_PRESENT=7, LOCKER_WIRE_DEVICE_EVENT_TYPE_CARD_OBSERVED=8, LOCKER_WIRE_DEVICE_EVENT_TYPE_SUPPLY_CHANGED=9, LOCKER_WIRE_DEVICE_EVENT_TYPE_HEALTH_CHANGED=10, LOCKER_WIRE_DEVICE_EVENT_TYPE_JOURNAL_FAULT=11, LOCKER_WIRE_DEVICE_EVENT_TYPE_COMMAND_ACCEPTED=12, LOCKER_WIRE_DEVICE_EVENT_TYPE_CONTEXT_RELEASED=13, LOCKER_WIRE_DEVICE_EVENT_TYPE_CONTINUITY_LOST=14, LOCKER_WIRE_DEVICE_EVENT_TYPE_UI_RESTART_REQUEST=15 };
enum { LOCKER_WIRE_DOOR_STATE_UNKNOWN=0, LOCKER_WIRE_DOOR_STATE_CLOSED=1, LOCKER_WIRE_DOOR_STATE_OPEN=2 };
enum { LOCKER_WIRE_ITEM_STATE_UNKNOWN=0, LOCKER_WIRE_ITEM_STATE_ABSENT=1, LOCKER_WIRE_ITEM_STATE_PRESENT=2 };
enum { LOCKER_WIRE_WIRE_RESULT_OK=0, LOCKER_WIRE_WIRE_RESULT_BAD_VERSION=1, LOCKER_WIRE_WIRE_RESULT_BAD_SESSION=2, LOCKER_WIRE_WIRE_RESULT_NOT_READY=3, LOCKER_WIRE_WIRE_RESULT_BAD_PRECONDITION=4, LOCKER_WIRE_WIRE_RESULT_REPLAY_BLOCKED=5, LOCKER_WIRE_WIRE_RESULT_ACTION_GAP=6, LOCKER_WIRE_WIRE_RESULT_BUSY=7, LOCKER_WIRE_WIRE_RESULT_LOG_UNAVAILABLE=8, LOCKER_WIRE_WIRE_RESULT_UNKNOWN_RESULT=9, LOCKER_WIRE_WIRE_RESULT_BAD_PAYLOAD=10, LOCKER_WIRE_WIRE_RESULT_CURSOR_GAP=11, LOCKER_WIRE_WIRE_RESULT_PHYSICAL_CHANGED=12, LOCKER_WIRE_WIRE_RESULT_JOURNAL_CHANGED=13, LOCKER_WIRE_WIRE_RESULT_UNSUPPORTED=14 };
enum { LOCKER_WIRE_WIRE_DEVICE_MODE_RECONCILING=0, LOCKER_WIRE_WIRE_DEVICE_MODE_READY=1, LOCKER_WIRE_WIRE_DEVICE_MODE_DEGRADED=2 };
enum { LOCKER_WIRE_WIRE_JOURNAL_STATE_UNINITIALIZED=0, LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY=1, LOCKER_WIRE_WIRE_JOURNAL_STATE_DEGRADED=2, LOCKER_WIRE_WIRE_JOURNAL_STATE_FAULT=3, LOCKER_WIRE_WIRE_JOURNAL_STATE_FULL=4 };
enum { LOCKER_WIRE_WIRE_SUPERVISOR_STATE_UNKNOWN=0, LOCKER_WIRE_WIRE_SUPERVISOR_STATE_RUNNING=1, LOCKER_WIRE_WIRE_SUPERVISOR_STATE_UI_RECOVERING=2, LOCKER_WIRE_WIRE_SUPERVISOR_STATE_LIMITED=3 };
#define LOCKER_CORE_SESSION_CREATE "SESSION_CREATE"
#define LOCKER_CORE_SESSION_REVOKE "SESSION_REVOKE"
#define LOCKER_CORE_SESSION_CURRENT "SESSION_CURRENT"
#define LOCKER_CORE_DEVICE_GET "DEVICE_GET"
#define LOCKER_CORE_SNAPSHOT_GET "SNAPSHOT_GET"
#define LOCKER_CORE_TOOLS_LIST "TOOLS_LIST"
#define LOCKER_CORE_TOOL_GET "TOOL_GET"
#define LOCKER_CORE_OPERATION_CREATE "OPERATION_CREATE"
#define LOCKER_CORE_OPERATION_GET "OPERATION_GET"
#define LOCKER_CORE_OPERATION_CONFIRM "OPERATION_CONFIRM"
#define LOCKER_CORE_OPERATION_CANCEL "OPERATION_CANCEL"
#define LOCKER_CORE_LOANS_LIST "LOANS_LIST"
#define LOCKER_CORE_EVENTS_LIST "EVENTS_LIST"
#define LOCKER_CORE_REVIEWS_LIST "REVIEWS_LIST"
#define LOCKER_CORE_OPERATION_RESOLVE "OPERATION_RESOLVE"
#define LOCKER_CORE_USERS_LIST "USERS_LIST"
#define LOCKER_CORE_USER_CREATE "USER_CREATE"
#define LOCKER_CORE_USER_UPDATE "USER_UPDATE"
#define LOCKER_CORE_CARD_OBSERVATIONS_LIST "CARD_OBSERVATIONS_LIST"
#define LOCKER_CORE_CARD_BIND "CARD_BIND"
#define LOCKER_CORE_CARD_UNBIND "CARD_UNBIND"
#define LOCKER_CORE_WS_SUBSCRIBE "WS_SUBSCRIBE"
#define LOCKER_CORE_VISION_STATUS_GET "VISION_STATUS_GET"
#define LOCKER_CORE_VISION_EVIDENCE_LIST "VISION_EVIDENCE_LIST"
#define LOCKER_CORE_PRESENCE_SUBMIT "PRESENCE_SUBMIT"
#define LOCKER_CORE_MAINTENANCE_RECONCILE "MAINTENANCE_RECONCILE"
typedef struct { locker_uuid_t session_id; uint32_t request_no; uint8_t message_type; uint64_t received_mono_ms; } mcu_request_context_t;
typedef struct { locker_door_state_t door; locker_item_state_t item; bool supply_valid; uint32_t supply_mv; uint64_t observed_mono_ms; } locker_board_inputs_t;
typedef struct { locker_uuid_t operation_id, command_id, session_id, journal_epoch; uint64_t expected_boot_id, expected_physical_revision, action_no, not_after_mcu_ms; uint16_t slot_id; locker_operation_kind_t kind; } locker_unlock_intent_t;
typedef struct { locker_uuid_t session_id, journal_epoch; uint64_t expected_boot_id, expected_physical_revision; locker_event_key_t persisted_through; } locker_ready_guard_t;
typedef struct { locker_uuid_t session_id, journal_epoch, operation_id, command_id; uint64_t expected_boot_id, expected_physical_revision; } locker_context_guard_t;
typedef struct { locker_uuid_t submission_token, request_id, operation_id; bool has_operation; locker_status_t status; } locker_receipt_t;
typedef struct { locker_uuid_t user_id, session_id; locker_role_t role; bool authenticated; } locker_principal_t;
typedef struct { locker_principal_t principal; locker_uuid_t request_id, idempotency_key, resource_id; uint64_t expected_revision; char kind[32]; char arguments_json[4097]; } locker_command_t;
typedef struct { locker_principal_t principal; locker_uuid_t request_id, resource_id; char kind[32], cursor[513]; uint16_t limit; } locker_query_t;
typedef struct { size_t length; char json[65536]; uint64_t snapshot_revision; locker_status_t status; } locker_query_result_t;
typedef struct { locker_event_key_t key; uint64_t mono_ms, physical_revision, detail; uint16_t event_type, reason, flags; locker_uuid_t operation_id, command_id; locker_door_state_t door; locker_item_state_t item; uint32_t supply_mv; uint8_t card_uid_len, card_uid[10]; bool live_transport; } locker_device_event_t;
typedef struct { locker_device_event_t events[4]; uint8_t count; bool has_more, gap_before_page; locker_event_key_t first_available, last_committed, next_after; } locker_event_batch_t;
typedef struct { locker_uuid_t host_instance_id, ui_instance_id; uint64_t core_progress, ui_progress; uint32_t core_age_ms, ui_age_ms; uint8_t supervisor_state; } locker_health_report_t;
enum { MCU_JOURNAL_RECOVER=0, MCU_JOURNAL_BARRIER=1, MCU_JOURNAL_FINALIZE=2, MCU_JOURNAL_RELEASE=3 };
typedef struct { uint32_t token, barrier_after_token; uint8_t kind; locker_uuid_t journal_epoch, session_id, operation_id, command_id; uint64_t expected_boot_id, action_no; locker_command_state_t terminal_state; } mcu_journal_control_request_t;
typedef struct { locker_uuid_t journal_epoch; uint64_t boot_id, action_watermark, dropped_events; locker_event_key_t first_available, last_committed, ack_persisted; locker_unlock_intent_t active_intent; locker_command_state_t command_state; uint8_t journal_state; bool has_active, continuity_ok; } mcu_journal_state_t;
typedef struct { locker_uuid_t instance_id; uint64_t progress, sampled_mono_ms; uint32_t observed_age_ms; uint8_t target; } locker_progress_t;
typedef struct { locker_uuid_t supervisor_instance_id; uint64_t sampled_mono_ms; bool service_known, ui_known, restart_count_known; locker_progress_t service, ui; locker_supervisor_state_t state; uint32_t restart_count; uint8_t window_attempts; uint32_t cooldown_remaining_ms; bool budget_ready; } locker_supervisor_health_t;
typedef struct { locker_uuid_t request_id, incident_id; char reason[257]; } locker_restart_request_t;
typedef struct { char id[65]; uint64_t snapshot_revision; char resource[16]; } locker_resource_change_t;
typedef struct { uint32_t uid, gid, pid; bool verified_ui_peer, verified_admin_peer; } locker_peer_t;
typedef struct { locker_principal_t principal; locker_uuid_t request_id, idempotency_key, operation_id; uint64_t operation_revision; } locker_presence_t;
typedef struct { locker_principal_t principal; locker_uuid_t request_id, idempotency_key, journal_epoch; uint64_t expected_boot_id, physical_revision, snapshot_revision; char reason[1025]; } locker_maintenance_t;
typedef struct { bool configured; uint8_t port_index; uint16_t pin_mask; uint8_t alternate_function; } locker_gpio_descriptor_t;
typedef struct { bool verified; locker_gpio_descriptor_t door, item, lock_output, alarm, rc522_cs, rc522_reset, supply_adc, uart_link_tx, uart_link_rx, uart_console_tx, uart_console_rx, flash_cs; uint8_t door_closed_level, item_present_level, lock_active_level, alarm_active_level; uint16_t debounce_ms, pulse_ms; uint8_t uart_link_instance, uart_console_instance, flash_spi_instance, rc522_spi_instance, adc_instance, adc_channel, lock_timer_instance, lock_timer_channel, hal_tick_timer_instance; uint32_t rk_uart_io_mv, host_input_mv, divider_upper_ohms, divider_lower_ohms, adc_reference_mv; } mcu_board_profile_t;
typedef struct { void *user; uint64_t (*monotonic_ms)(void *user); bool (*wall_utc_ms)(void *user, uint64_t *out); locker_status_t (*new_uuid)(void *user, locker_uuid_t *out); } locker_clock_port_t;
typedef struct { size_t length; char json[4097]; } locker_vision_policy_t;
typedef struct { size_t length; char json[8193]; } locker_vision_request_t;
typedef struct { size_t length; char json[16385]; uint64_t received_mono_ms; } locker_vision_result_t;
typedef struct { size_t length; char json[4097]; uint64_t received_mono_ms; } locker_vision_status_t;
typedef struct { char socket_path[108]; uint32_t worker_uid, queue_capacity; locker_uuid_t host_instance_id; locker_clock_port_t clock; } locker_vision_config_t;
typedef struct { void *user; locker_status_t (*result)(void *user, const locker_vision_result_t *result); locker_status_t (*status)(void *user, const locker_vision_status_t *status); } locker_vision_sink_t;
typedef struct { bool simulated; char path[108], expected_device_id[25]; uint32_t baud_rate; locker_uuid_t host_instance_id; locker_clock_port_t clock; } locker_device_config_t;
typedef struct { size_t token_length; char token[129]; } locker_auth_request_t;
typedef struct { char path[256]; uint32_t schema_version; bool initialize_empty; } locker_repo_config_t;
typedef struct { uint8_t schema_version; char collection[32], key[193], after_key[193]; uint16_t limit; } locker_repo_query_t;
typedef struct { size_t length; char json[65536]; } locker_change_t;
typedef struct { size_t length; char json[65536]; uint64_t snapshot_revision; } locker_repo_result_t;
typedef struct { size_t length; char json[65536]; uint64_t snapshot_revision; } locker_recovery_t;
typedef struct { char cabinet_name[257]; locker_uuid_t instance_id; locker_clock_port_t clock; uint32_t queue_capacity, completion_retention_ms; locker_vision_policy_t vision_policy; } locker_core_config_t;
typedef struct { char ui_executable[256], service_socket[108], supervisor_socket[108], checkpoint_path[256]; uint32_t ui_uid, service_uid; bool recovery_enabled; } locker_supervisor_config_t;
typedef struct { char bind_address[64], certificate_path[256], private_key_path[256]; uint16_t port, worker_count; } locker_http_config_t;
typedef struct { char socket_path[108]; uint32_t allowed_ui_uid, allowed_admin_uid, file_mode; } locker_ipc_config_t;
typedef struct { bool board_verified; uint16_t pulse_ms; uint32_t prepare_deadline_ms; } mcu_control_config_t;
typedef struct { bool geometry_verified; uint32_t jedec_id, capacity_bytes, sector_bytes; uint16_t page_bytes; uint8_t chip_select; } mcu_journal_config_t;
typedef struct { uint32_t task_deadline_ms[4], host_timeout_ms; } mcu_health_config_t;
/* Only module contexts remain opaque. Cross-module data above is concrete. */
/* Decoded native DTOs: never send sizeof(struct) bytes to UART. */
typedef struct { uint64_t boot_id, event_seq; } locker_wire_cursor_t;
typedef struct { uint64_t boot_id; uint64_t event_seq; uint64_t mono_ms; uint16_t event_type; uint16_t reason; locker_uuid_t operation_id; locker_uuid_t command_id; uint8_t door; uint8_t item; uint16_t flags; uint32_t supply_mv; uint64_t physical_revision; uint64_t detail; uint8_t card_uid_len; uint8_t card_uid[10]; uint8_t reserved; } locker_wire_event96_t;
typedef struct { locker_uuid_t session_id; locker_uuid_t new_session_id; locker_uuid_t host_instance_id; uint8_t requested_version; } locker_wire_hello_request_t;
typedef struct { locker_uuid_t session_id; uint16_t result; uint8_t device_uid[12]; locker_uuid_t journal_epoch; uint64_t boot_id; uint8_t firmware_major; uint8_t firmware_minor; uint8_t firmware_patch; uint32_t capabilities; uint8_t device_mode; uint8_t journal_state; uint64_t action_watermark; } locker_wire_hello_response_t;
typedef struct { locker_uuid_t session_id; } locker_wire_get_snapshot_request_t;
typedef struct { locker_uuid_t session_id; uint16_t result; locker_uuid_t journal_epoch; uint64_t boot_id; uint64_t sampled_mono_ms; uint64_t physical_revision; uint8_t door; uint8_t item; uint32_t supply_mv; uint16_t flags; uint64_t dropped_events; locker_wire_cursor_t first_available; locker_wire_cursor_t last_committed; locker_wire_cursor_t ack_persisted; locker_uuid_t active_operation_id; locker_uuid_t active_command_id; uint8_t active_command_state; locker_uuid_t active_session_id; uint64_t active_action_no; uint64_t action_watermark; uint8_t device_mode; uint8_t journal_state; } locker_wire_get_snapshot_response_t;
typedef struct { locker_uuid_t session_id; locker_uuid_t journal_epoch; locker_wire_cursor_t after; uint8_t max_count; } locker_wire_fetch_events_request_t;
typedef struct { locker_uuid_t session_id; uint16_t result; locker_uuid_t journal_epoch; locker_wire_cursor_t first_available; locker_wire_cursor_t last_committed; locker_wire_cursor_t next_after; uint8_t event_count; uint8_t has_more; uint8_t gap_before_page; locker_wire_event96_t events[4]; } locker_wire_fetch_events_response_t;
typedef struct { locker_uuid_t session_id; locker_uuid_t journal_epoch; locker_wire_cursor_t through; } locker_wire_ack_persisted_request_t;
typedef struct { locker_uuid_t session_id; uint16_t result; locker_wire_cursor_t ack_persisted; } locker_wire_ack_persisted_response_t;
typedef struct { locker_uuid_t session_id; locker_uuid_t journal_epoch; uint64_t expected_boot_id; uint64_t expected_physical_revision; locker_wire_cursor_t persisted_through; } locker_wire_resume_ready_request_t;
typedef struct { locker_uuid_t session_id; uint16_t result; uint64_t physical_revision; uint8_t device_mode; } locker_wire_resume_ready_response_t;
typedef struct { locker_uuid_t session_id; uint16_t reason; } locker_wire_disarm_request_t;
typedef struct { locker_uuid_t session_id; uint16_t result; uint8_t device_mode; uint64_t physical_revision; } locker_wire_disarm_response_t;
typedef struct { locker_uuid_t session_id; locker_uuid_t journal_epoch; uint64_t expected_boot_id; uint64_t expected_physical_revision; uint64_t action_no; uint64_t not_after_mcu_ms; locker_uuid_t operation_id; locker_uuid_t command_id; uint16_t slot_id; uint8_t kind; } locker_wire_execute_unlock_request_t;
typedef struct { locker_uuid_t session_id; uint16_t result; locker_uuid_t operation_id; locker_uuid_t command_id; uint8_t command_state; uint64_t action_no; locker_uuid_t command_session_id; uint64_t command_boot_id; uint64_t physical_revision; uint16_t reason; } locker_wire_execute_unlock_response_t;
typedef struct { locker_uuid_t session_id; locker_uuid_t journal_epoch; locker_uuid_t command_id; } locker_wire_query_command_request_t;
typedef struct { locker_uuid_t session_id; uint16_t result; locker_uuid_t operation_id; locker_uuid_t command_id; uint8_t command_state; uint64_t action_no; locker_uuid_t command_session_id; uint64_t command_boot_id; uint64_t physical_revision; uint16_t reason; } locker_wire_query_command_response_t;
typedef struct { locker_uuid_t session_id; locker_uuid_t host_instance_id; locker_uuid_t ui_instance_id; uint64_t core_progress; uint64_t ui_progress; uint32_t core_observed_age_ms; uint32_t ui_observed_age_ms; uint8_t supervisor_state; } locker_wire_host_health_request_t;
typedef struct { locker_uuid_t session_id; uint16_t result; uint64_t received_mono_ms; uint8_t device_mode; } locker_wire_host_health_response_t;
typedef struct { locker_uuid_t session_id; uint32_t anchor_id; uint64_t host_utc_ms; uint64_t host_monotonic_ms; } locker_wire_time_anchor_request_t;
typedef struct { locker_uuid_t session_id; uint16_t result; uint32_t anchor_id; uint64_t boot_id; uint64_t mcu_receive_mono_ms; uint64_t mcu_send_mono_ms; } locker_wire_time_anchor_response_t;
typedef struct { locker_uuid_t session_id; locker_uuid_t journal_epoch; uint64_t expected_boot_id; uint64_t expected_physical_revision; locker_uuid_t operation_id; locker_uuid_t command_id; } locker_wire_release_context_request_t;
typedef struct { locker_uuid_t session_id; uint16_t result; uint8_t already_released; uint64_t physical_revision; } locker_wire_release_context_response_t;
typedef struct { uint32_t request_no; uint8_t message_type; uint64_t request_started_host_mono_ms, received_host_mono_ms; union { locker_wire_hello_response_t hello; locker_wire_get_snapshot_response_t get_snapshot; locker_wire_fetch_events_response_t fetch_events; locker_wire_ack_persisted_response_t ack_persisted; locker_wire_resume_ready_response_t resume_ready; locker_wire_disarm_response_t disarm; locker_wire_execute_unlock_response_t execute_unlock; locker_wire_query_command_response_t query_command; locker_wire_host_health_response_t host_health; locker_wire_time_anchor_response_t time_anchor; locker_wire_release_context_response_t release_context; } body; } locker_device_reply_t;
typedef struct { void *user; locker_status_t (*reply)(void *user, const locker_device_reply_t *reply); locker_status_t (*event)(void *user, const locker_device_event_t *event); } locker_device_sink_t;
typedef struct { void *user; locker_status_t (*reply)(void *user, const locker_device_reply_t *reply); } mcu_control_sink_t;
typedef struct { void *user; locker_status_t (*committed)(void *user, uint32_t token, locker_status_t result); locker_status_t (*event_committed)(void *user, const locker_device_event_t *event); locker_status_t (*control_result)(void *user, uint32_t token, locker_status_t result, const mcu_journal_state_t *state); } mcu_journal_sink_t;
typedef struct locker_core locker_core_t;
typedef struct locker_device locker_device_t;
typedef struct locker_http locker_http_t;
typedef struct locker_ipc locker_ipc_t;
typedef struct locker_publisher locker_publisher_t;
typedef struct locker_repo locker_repo_t;
typedef struct locker_supervisor locker_supervisor_t;
typedef struct locker_vision locker_vision_t;
typedef struct mcu_control mcu_control_t;
typedef struct mcu_health mcu_health_t;
typedef struct mcu_journal mcu_journal_t;
/* Host-only JSON DTO buffers must not be instantiated as MCU queues or allocated on small stacks. */
/* Repo JSON fields follow internal-data.schema.json, not arbitrary SQL or public API payloads. */
/* RK Core: 复制入有界队列；receipt含request/operation关联，排队不表示业务或物理完成。 */
locker_status_t locker_core_submit(locker_core_t *core, const locker_command_t *command, locker_receipt_t *receipt);
/* RK Core completion port: HTTP/IPC按Core生成的内部submission_token最多等待1000ms；不按客户端request_id寻址。超时不取消，复用原幂等键。 */
locker_status_t locker_core_wait_result(locker_core_t *core, const locker_uuid_t *submission_token, uint32_t timeout_ms, locker_query_result_t *result);
/* RK Core: 事件键去重后在业务线程应用；补传不能产生柜前证明。 */
locker_status_t locker_core_apply_device_event(locker_core_t *core, const locker_device_event_t *event);
/* RK Core: 设备适配器提交已解码HELLO/快照/命令结果/水位/健康响应；拷贝入队并按request/session匹配，不能用DEVICE_EVENT替代响应。 */
locker_status_t locker_core_apply_device_reply(locker_core_t *core, const locker_device_reply_t *reply);
/* RK Core: 推进业务超时与真实核心进度，不等待外设。 */
locker_status_t locker_core_tick(locker_core_t *core, uint64_t monotonic_ms);
/* RK Core: Bounded HTTP/IPC wrapper: enqueue to Core, use private submission_token and wait <=1000ms; never read mutable domain or repository on caller thread. */
locker_status_t locker_core_query(locker_core_t *core, const locker_query_t *query, locker_query_result_t *result);
/* RK Storage: 操作+outbox+幂等+revision同一事务；仅业务线程调用。 */
locker_status_t locker_repo_commit_change(locker_repo_t *repo, const locker_change_t *change);
/* RK Storage: 新事件去重、可选投影变更和连续游标同事务；投影由Core构造，不由Repo猜业务。 */
locker_status_t locker_repo_import_events(locker_repo_t *repo, const locker_event_batch_t *batch, const locker_change_t *projection_change, locker_event_key_t *committed_cursor);
/* RK Storage: 按集合/页恢复；结果DTO字段由internal-data.schema.json固定，不一次加载无限历史。 */
locker_status_t locker_repo_load_recovery(locker_repo_t *repo, const locker_repo_query_t *query, locker_recovery_t *result);
/* RK Device: 真实串口或显式模拟字节流；创建新会话并禁止新动作直至核对。 */
locker_status_t locker_device_open(locker_device_t *device, const locker_device_config_t *config);
/* RK Device: 异步取得持久化屏障快照。 */
locker_status_t locker_device_get_snapshot(locker_device_t *device, uint32_t request_no);
/* RK Device: 每物理command一次发送，超时查询，禁止自动重发。 */
locker_status_t locker_device_execute_once(locker_device_t *device, const locker_unlock_intent_t *intent);
/* RK Device: 无副作用结果查询；未知不是未执行。 */
locker_status_t locker_device_query_command(locker_device_t *device, const locker_uuid_t *command_id);
/* RK Device: limit<=4，返回连续范围和缺口。 */
locker_status_t locker_device_fetch_events(locker_device_t *device, const locker_event_key_t *after, uint8_t limit);
/* RK Device: 只能确认RK已提交的连续水位。 */
locker_status_t locker_device_ack_persisted(locker_device_t *device, const locker_event_key_t *cursor);
/* RK Device: 对物理版本、日志epoch和水位比较；不是无条件ARM。 */
locker_status_t locker_device_resume_ready(locker_device_t *device, const locker_ready_guard_t *guard);
/* RK Device: 仅释放匹配的闭门上下文，不开锁、不改借用。 */
locker_status_t locker_device_release_context(locker_device_t *device, const locker_context_guard_t *guard);
/* RK Device: 携带真实实例/进度/年龄，适配器不得代增进度。 */
locker_status_t locker_device_send_health(locker_device_t *device, const locker_health_report_t *report);
/* RK Events: 有界非阻塞；满则客户端重新同步。 */
locker_status_t locker_events_try_publish(locker_publisher_t *publisher, const locker_resource_change_t *change);
/* RK Local IPC: 私有SO_PEERCRED+登录会话+owner+revision；不能从HTTP调用。 */
locker_status_t locker_local_presence_submit(locker_core_t *core, const locker_peer_t *peer, const locker_presence_t *presence, locker_receipt_t *receipt);
/* Supervisor: 只接受service/UI可信peer的真实循环进度。 */
locker_status_t locker_supervisor_report_progress(locker_supervisor_t *supervisor, const locker_progress_t *progress);
/* Supervisor: 固定UI目标，限次冷却，无shell与锁动作。 */
locker_status_t locker_supervisor_request_ui_restart(locker_supervisor_t *supervisor, const locker_restart_request_t *request);
/* MCU Control: 排队->检查->异步提交意图->再检查->硬件定时脉冲。 */
locker_status_t mcu_control_submit_unlock(mcu_control_t *control, const mcu_request_context_t *request, const locker_unlock_intent_t *intent);
/* MCU Control: 去抖、UNKNOWN、物理版本和连续性；实际电平由BSP转换。 */
locker_status_t mcu_control_observe_inputs(mcu_control_t *control, const locker_board_inputs_t *inputs);
/* MCU Control: 只有提交成功且2000ms准备期限内、guard仍成立才能驱动。 */
locker_status_t mcu_control_on_journal_result(mcu_control_t *control, uint32_t token, locker_status_t result, const mcu_journal_state_t *state);
/* MCU Control: 捕获物理状态并等待此前事件提交，异步回屏障。 */
locker_status_t mcu_control_make_snapshot(mcu_control_t *control, const mcu_request_context_t *request);
/* MCU Control: 闭门、无脉冲、context/revision匹配才释放。 */
locker_status_t mcu_control_release_context(mcu_control_t *control, const mcu_request_context_t *request, const locker_context_guard_t *guard);
/* MCU Journal: 复制入队；完成通过token回执；队列满锁定连续性故障。 */
locker_status_t mcu_journal_append(mcu_journal_t *journal, const locker_device_event_t *event, uint32_t token);
/* MCU Journal: 意图、水位、上下文作为同一持久化检查点提交。 */
locker_status_t mcu_journal_commit_intent(mcu_journal_t *journal, const locker_unlock_intent_t *intent, uint32_t token);
/* MCU Journal: 返回最多4条和边界，不能伪造连续性。 */
locker_status_t mcu_journal_read_page(mcu_journal_t *journal, const locker_event_key_t *after, uint8_t limit, locker_event_batch_t *result);
/* MCU Journal: 推进已确认水位，保留未决动作检查点。 */
locker_status_t mcu_journal_ack(mcu_journal_t *journal, const locker_event_key_t *cursor);
/* MCU BSP: 非阻塞规范化读数；未配置或不稳定返回UNKNOWN。 */
locker_status_t mcu_board_read_inputs(locker_board_inputs_t *result);
/* MCU BSP: 仅Control调用；硬件截止<=1000ms且不超过锁实际允许值。 */
locker_status_t mcu_board_start_bounded_pulse(uint16_t pulse_ms);
/* MCU BSP: 复位/异常优先关断；不等待文件系统或串口。 */
void mcu_board_force_off(void);
/* MCU BSP: 仅PeripheralStorage；有限长度/时间，不把结构体作为wire。 */
locker_status_t mcu_board_spi_transfer(uint8_t chip_select, const uint8_t *tx, uint8_t *rx, uint16_t length, uint32_t timeout_us);
/* MCU Health: 任务真实进度，不能只观察定时中断。 */
locker_status_t mcu_health_report_progress(mcu_health_t *health, uint8_t task_id, uint64_t progress);
/* MCU Health: 只在本地关键任务健康时允许喂狗；主机离线本身不停止记录。 */
locker_status_t mcu_health_evaluate(mcu_health_t *health, uint64_t monotonic_ms);
/* RK Storage: Schema-checked open; failure sets out to NULL; never clear corruption. */
locker_status_t locker_repo_open(const locker_repo_config_t *config, locker_repo_t **out);
/* RK Storage: Typed RepositoryQuery/Result; core-thread-only; byte-bounded pagination. */
locker_status_t locker_repo_query(locker_repo_t *repo, const locker_repo_query_t *query, locker_repo_result_t *result);
/* RK Storage: Close after core stops; NULL-safe; no concurrent transaction. */
void locker_repo_close(locker_repo_t *repo);
/* RK Runtime/Core: Dependencies are borrowed; fail with NULL; complete recovery before business readiness. */
locker_status_t locker_core_create(const locker_core_config_t *config, locker_repo_t *repo, locker_device_t *device, locker_publisher_t *publisher, locker_core_t **out);
/* RK Core: Start one owner thread; remain RECONCILING until synchronized. */
locker_status_t locker_core_start(locker_core_t *core);
/* RK Core: Stop admission and drain or mark writes; timeout is not cancellation. */
locker_status_t locker_core_stop(locker_core_t *core, uint32_t timeout_ms);
/* RK Core: Only after successful stop and adapter callbacks cease; do not close borrowed dependencies. */
void locker_core_destroy(locker_core_t *core);
/* RK Auth/Core: Validate current session/user in core; recheck session at command execution. */
locker_status_t locker_core_authenticate(locker_core_t *core, const locker_auth_request_t *request, locker_principal_t *result);
/* RK Device: Allocate unopened context; failure sets NULL. */
locker_status_t locker_device_create(locker_device_t **out);
/* RK Device: Stop IO and callbacks; invalidate session without replaying EXECUTE. */
locker_status_t locker_device_close(locker_device_t *device);
/* RK Device: Free only after close and thread join. */
void locker_device_destroy(locker_device_t *device);
/* RK Device: Capture host request start and response arrival; normalize TIME_ANCHOR every 2 seconds. */
locker_status_t locker_device_time_anchor(locker_device_t *device, uint32_t request_no);
/* RK Events: Create bounded change publisher; failure sets NULL. */
locker_status_t locker_events_create(locker_publisher_t **out);
/* RK Events: Free after websocket closure and producer stop. */
void locker_events_destroy(locker_publisher_t *publisher);
/* Supervisor: Fixed UI target and private IPC; independent restart-limit checkpoint. */
locker_status_t locker_supervisor_create(const locker_supervisor_config_t *config, locker_supervisor_t **out);
/* Supervisor: Release after supervisor loop stops; no ledger writes. */
void locker_supervisor_destroy(locker_supervisor_t *supervisor);
/* MCU Control: Return module-owned static context; unverified board config permits diagnostics only. */
locker_status_t mcu_control_init(const mcu_control_config_t *config, mcu_journal_t *journal, mcu_control_t **out);
/* MCU Journal: Static context; no automatic format; failed boot checkpoint forbids READY. */
locker_status_t mcu_journal_init(const mcu_journal_config_t *config, mcu_journal_t **out);
/* MCU Health: Static context; valid task deadlines required; host offline does not stop MCU. */
locker_status_t mcu_health_init(const mcu_health_config_t *config, mcu_health_t **out);
/* RK Device: Before open; borrowed sink lives through close; callbacks borrow DTO only for call, receiver must copy; backpressure stops new execution/health until reconciliation. */
locker_status_t locker_device_bind_sink(locker_device_t *device, const locker_device_sink_t *sink);
/* RK HTTP: Start HTTPS/WS only; no local presence routes; dependencies borrowed. */
locker_status_t locker_http_start(const locker_http_config_t *config, locker_core_t *core, locker_publisher_t *publisher, locker_http_t **out);
/* RK HTTP: Stop admission and join workers; timeout forbids destroy. */
locker_status_t locker_http_stop(locker_http_t *http, uint32_t timeout_ms);
/* RK HTTP: After successful stop; do not release borrowed core/publisher. */
void locker_http_destroy(locker_http_t *http);
/* RK IPC: Restricted seqpacket service socket; verify peer credentials; dependencies borrowed. */
locker_status_t locker_ipc_start(const locker_ipc_config_t *config, locker_core_t *core, locker_ipc_t **out);
/* RK IPC: Stop admission and join workers before destroying core. */
locker_status_t locker_ipc_stop(locker_ipc_t *ipc, uint32_t timeout_ms);
/* RK IPC: After successful stop; only remove owned socket path. */
void locker_ipc_destroy(locker_ipc_t *ipc);
/* MCU Journal: Before storage task starts; borrowed static sink; callback queues token/result to Control, never drives GPIO. */
locker_status_t mcu_journal_bind_sink(mcu_journal_t *journal, const mcu_journal_sink_t *sink);
/* MCU HAL BSP: HAL-only private binding; reject unverified pin/polarity/ADC/timer/IO settings, default lock off; no public network configuration. */
locker_status_t mcu_board_apply_profile(const mcu_board_profile_t *profile);
/* MCU HAL BSP: Nonblocking LED/buzzer pattern through verified HAL pins; does not drive lock or mutate custody. */
locker_status_t mcu_board_set_alarm(locker_alarm_pattern_t pattern);
/* RK Local Admin: Trusted local admin CLI and current admin session; compare snapshot/epoch/boot/physical revision and ledger; clear only authorized hold, never pulse or hide gaps. */
locker_status_t locker_local_maintenance_reconcile(locker_core_t *core, const locker_peer_t *peer, const locker_maintenance_t *request, locker_receipt_t *receipt);
/* Supervisor: Read immutable local supervision snapshot; no process action. Restricted health.get RPC exposes this only to the registered service peer. */
locker_status_t locker_supervisor_get_health(locker_supervisor_t *supervisor, locker_supervisor_health_t *result);
/* RK Core: Copy observation into Core queue; only Core updates public projection; stale cache never refreshes sample time. */
locker_status_t locker_core_apply_supervisor_health(locker_core_t *core, const locker_supervisor_health_t *health);
/* RK Core: Nonblocking immutable observation of real Core loop; no queue wait, IO or synthetic progress increment. */
locker_status_t locker_core_get_progress(locker_core_t *core, locker_progress_t *result);
/* RK Vision: Create unopened local adapter; no inference inside Core. */
locker_status_t locker_vision_create(const locker_vision_config_t *config, locker_vision_t **out);
/* RK Vision: Bind before open; callbacks borrow DTO only, Core receiver copies; sink lives until successful close. */
locker_status_t locker_vision_bind_sink(locker_vision_t *vision, const locker_vision_sink_t *sink);
/* RK Vision: Start bounded IO/HELLO; local worker can be unavailable without blocking Core. */
locker_status_t locker_vision_open(locker_vision_t *vision);
/* RK Vision: Validate and copy one persisted capture request to bounded queue; QUEUED is not evidence. */
locker_status_t locker_vision_submit(locker_vision_t *vision, const locker_vision_request_t *request);
/* RK Vision: Nonblocking cached raw worker observation; source time never refreshed by reads. */
locker_status_t locker_vision_get_status(locker_vision_t *vision, locker_vision_status_t *result);
/* RK Vision: Stop IO, invalidate captures and join callbacks; timeout forbids destroy, never replay requests. */
locker_status_t locker_vision_close(locker_vision_t *vision, uint32_t timeout_ms);
/* RK Vision: After successful close and Core producer stop only. */
void locker_vision_destroy(locker_vision_t *vision);
/* RK Core: Before Core start; borrowed adapter lives until Core stop and all callbacks cease. */
locker_status_t locker_core_bind_vision(locker_core_t *core, locker_vision_t *vision);
/* RK Core: Copy validated raw result into Core queue; Core revalidates correlation, clocks, guards and commits evidence; no worker verdict trusted. */
locker_status_t locker_core_apply_vision_result(locker_core_t *core, const locker_vision_result_t *result);
/* RK Core: Copy source worker observation into Core queue; unknown/stale in REQUIRED fails closed, never synthetic READY. */
locker_status_t locker_core_apply_vision_status(locker_core_t *core, const locker_vision_status_t *status);
/* MCU Control/Communication: Bind before tasks start. Async reply preserves copied session/request_no/type; callback queues to Communication, never GPIO; dependencies static until tasks stop. */
locker_status_t mcu_control_bind_sink(mcu_control_t *control, const mcu_control_sink_t *sink);
/* MCU Journal: Bounded queue RECOVER/BARRIER/FINALIZE/RELEASE. Storage owns metadata/checkpoints; control_result returns committed immutable state; no format or watermark rollback. */
locker_status_t mcu_journal_control(mcu_journal_t *journal, const mcu_journal_control_request_t *request);
#ifdef __cplusplus
}
#endif
#endif
