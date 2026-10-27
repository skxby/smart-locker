# S1-03 mutation batch: one deliberate defect per point, run the full CTest suite.
# A point is CAUGHT when ctest fails; NOT_CAUGHT points are reported as-is (no fake passes).
import os, shutil, subprocess, hashlib, re, sys, time

# B-16: never hard-code the source root. Priority: --src <dir>, $S103_SRC, then
# the current working directory. The workspace layout is
# locker-workspace/{src,build,logs,artifacts,runtime}; mutation sandboxes live
# under build/, logs under logs/, so nothing is written to $HOME directly.
def _arg_value(flag):
    if flag in sys.argv:
        i = sys.argv.index(flag)
        if i + 1 < len(sys.argv):
            return sys.argv[i + 1]
    return None

SRC = os.path.abspath(_arg_value('--src') or os.environ.get('S103_SRC') or os.getcwd())
WS = os.path.abspath(os.environ.get('S103_WORKSPACE') or os.path.join(os.path.expanduser('~'), 'locker-workspace'))
RUN = os.environ.get('S103_RUN') or 'manual'
BASE = os.path.join(WS, 'build', 'S1-03', 'mutation', RUN)
LOGDIR = os.path.join(WS, 'logs', 'S1-03', 'mutation', RUN)
OUT = os.path.join(SRC, 'docs/evidence/stage-01/S1-03/mutation.log')
RAW_DIR = os.path.join(LOGDIR, 'raw')
CHECK_ONLY = '--check' in sys.argv
if not os.path.isdir(SRC):
    print('SOURCE_ROOT_MISSING %s' % SRC)
    sys.exit(2)

def sh(cmd, cwd=None):
    return subprocess.run(['bash', '-lc', cmd], capture_output=True, text=True, cwd=cwd)

def sha(p):
    return hashlib.sha256(open(p, 'rb').read()).hexdigest()[:12]

def line_of(path, needle):
    with open(path, 'r', encoding='utf-8') as f:
        for i, l in enumerate(f, 1):
            if needle in l:
                return i
    return -1

# (name, audit_ref, relpath, old, new, anchor_needle)
POINTS = [
 ('m1_crc_poly', 'wire.c:26 CRC-16/CCITT-FALSE poly', 'host/device/src/wire.c',
  'crc = (uint16_t)((uint16_t)(crc << 1) ^ LOCKER_WIRE_CRC_POLY);',
  'crc = (uint16_t)(crc << 1);',
  'LOCKER_WIRE_CRC_POLY'),

 ('m2_magic', 'wire.c:196 magic resync', 'host/device/src/wire.c',
  'if (stream->buffer[0] != LOCKER_WIRE_MAGIC_FIRST ||\n                stream->buffer[1] != LOCKER_WIRE_MAGIC_SECOND) {',
  'if (0) {',
  'LOCKER_WIRE_MAGIC_SECOND'),

 ('m3_oversize_len', 'wire.c:205 payload_len>512 resync', 'host/device/src/wire.c',
  'payload_length = wire_get_u16(&stream->buffer[4]);\n            if (payload_length > LOCKER_WIRE_MAX_PAYLOAD) {',
  'payload_length = wire_get_u16(&stream->buffer[4]);\n            if (0) {',
  'payload_length = wire_get_u16(&stream->buffer[4]);'),

 ('m4_crc_mismatch', 'wire.c:219 CRC mismatch resync', 'host/device/src/wire.c',
  'if (actual_crc != expected_crc) {',
  'if (0) {',
  'actual_crc != expected_crc'),

 ('m5_reqno_zero', 'wire.c:98 request_no!=0 for requests', 'host/device/src/wire.c',
  'if (type != LOCKER_WIRE_DEVICE_EVENT && request_no == 0u) {',
  'if (0) {',
  'request_no == 0u) {'),

 ('m6_event_reqno', 'wire.c:95 DEVICE_EVENT request_no==0', 'host/device/src/wire.c',
  'if (type == LOCKER_WIRE_DEVICE_EVENT && request_no != 0u) {',
  'if (0) {',
  'request_no != 0u) {'),

 ('m7_pending_type', 'port.c:134 a stale request number of another type must not take the live slot', 'host/device/src/port.c',
  '            entry->request_type == request_type &&',
  '            (entry->request_type == request_type || 1) &&',
  'entry->request_type == request_type &&'),

 ('m8_event_session', 'port.c:362 DEVICE_EVENT session filter', 'host/device/src/port.c',
  '            !locker_uuid_equal(&event_session, &device->session_id) ||',
  '            (!locker_uuid_equal(&event_session, &device->session_id) && 0) ||',
  '!locker_uuid_equal(&event_session, &device->session_id) ||'),

 ('m9_reply_flag', 'port.c:429 reply.message_type carries response flag', 'host/device/src/port.c',
  'reply.message_type = (uint8_t)(request_type | LOCKER_WIRE_RESPONSE_FLAG);',
  'reply.message_type = request_type;',
  'reply.message_type = (uint8_t)(request_type |'),

 ('m10_simulated_gate', 'port.c:612 stage-1 simulated-only gate', 'host/device/src/port.c',
  'if (!config->simulated) {',
  'if (0) {',
  'config->simulated'),

 ('m11_slot_id', 'port.c:703 slot_id must be 1', 'host/device/src/port.c',
  'if (intent->slot_id != 1u) {',
  'if (0) {',
  'intent->slot_id != 1u'),

 ('m12_fetch_limit', 'port.c:784 fetch limit 1..4', 'host/device/src/port.c',
  'if (limit == 0u || limit > 4u) {',
  'if (0) {',
  'limit == 0u || limit > 4u'),

 ('m13_anchor_reqno', 'port.c:924 time_anchor rejects request_no==0', 'host/device/src/port.c',
  'locker_status_t locker_device_time_anchor(locker_device_t *device, uint32_t request_no)\n{\n    locker_wire_request_union_t request;\n\n    if (device == NULL || !device->created || request_no == 0u) {',
  'locker_status_t locker_device_time_anchor(locker_device_t *device, uint32_t request_no)\n{\n    locker_wire_request_union_t request;\n\n    if (device == NULL || !device->created || (request_no == 0u && 0)) {',
  'locker_device_time_anchor'),

 ('m14_cursor_halfzero', 'codec.c:209 half-zero cursor is invalid', 'host/device/src/codec.c',
  '    if (cursor->boot_id == 0u) {\n        return cursor->event_seq == 0u;\n    }\n    return cursor->event_seq != 0u;',
  '    return true;',
  'locker_wire_cursor_valid'),

 ('m15_event_type_range', 'codec.c:294 event_type 1..15', 'host/device/src/codec.c',
  'if (record->event_type < 1u || record->event_type > 15u) {',
  'if (0) {',
  'record->event_type > 15u'),

 ('m16_card_type', 'codec.c:312 card_uid only on CARD_OBSERVED', 'host/device/src/codec.c',
  'if (record->event_type != 8u) {',
  'if (0) {',
  'record->event_type != 8u'),

 ('m17_reserved', 'codec.c:324 reserved must be 0', 'host/device/src/codec.c',
  'if (record->reserved != 0u) {',
  'if (0) {',
  'record->reserved != 0u'),

 ('m18_supply_unknown', 'codec.c:327 SUPPLY_VALID forbids 0xffffffff', 'host/device/src/codec.c',
  'if ((record->flags & LOCKER_WIRE_FLAG_SUPPLY_VALID) != 0u &&\n        record->supply_mv == LOCKER_WIRE_SUPPLY_UNKNOWN) {',
  'if (0) {',
  'LOCKER_WIRE_SUPPLY_UNKNOWN) {'),

 ('m19_decode_version', 'codec.c:1169 response version check', 'host/device/src/codec.c',
  '    if (frame->version != LOCKER_WIRE_VERSION) {\n        return LOCKER_WIRE_RESULT_BAD_VERSION;\n    }\n    if ((frame->type & LOCKER_WIRE_RESPONSE_FLAG) == 0u) {\n        return LOCKER_WIRE_RESULT_UNSUPPORTED;\n    }',
  '    if ((frame->type & LOCKER_WIRE_RESPONSE_FLAG) == 0u) {\n        return LOCKER_WIRE_RESULT_UNSUPPORTED;\n    }',
  'locker_wire_decode_response'),

 ('m20_failed_len', 'codec.c:1191 failed response is exactly 18 bytes', 'host/device/src/codec.c',
  'if (frame->payload_len != RESPONSE_PREFIX_SIZE) {',
  'if (0) {',
  'RESPONSE_PREFIX_SIZE) {'),

 ('m21_fetch_len', 'codec.c:1221 fetch length re-check after decode', 'host/device/src/codec.c',
  'if (frame->payload_len !=\n            locker_wire_response_payload_size(type, out_response->fetch_events.event_count)) {',
  'if (0) {',
  'out_response->fetch_events.event_count))'),

 ('m22_queue_full', 'sim.c:851 full live queue drops the live copy, journal kept', 'host/simulator/src/sim.c',
  'if (sim->queue_count >= SIM_DEVICE_EVENT_QUEUE) {',
  'if (0) {',
  'sim->queue_count >= SIM_DEVICE_EVENT_QUEUE'),

 ('m23_queue_no_session', 'sim.c:847 no session means journal-only (no live push)', 'host/simulator/src/sim.c',
  '    if (!sim->has_session) {\n        /* uart-v1.md:356: without a session the record is journaled only. */\n        return;\n    }',
  '    if (0) {\n        return;\n    }',
  'without a session the record is journaled only'),

 ('m24_hello_version', 'sim.c:1114 HELLO requested_version must be 1', 'host/simulator/src/sim.c',
  'if (request->requested_version != LOCKER_WIRE_VERSION) {',
  'if (0) {',
  'requested_version != LOCKER_WIRE_VERSION'),

 ('m25_has_more', 'sim.c:1276 has_more from next_after vs last_committed', 'host/simulator/src/sim.c',
  'page->has_more = sim_cursor_compare(page->next_after, sim->last_committed) < 0;',
  'page->has_more = false;',
  'page->has_more = sim_cursor_compare'),

 ('m26_fetch_after_bound', 'sim.c:1297 after beyond last_committed is BAD_PAYLOAD', 'host/simulator/src/sim.c',
  'if (sim_cursor_compare(request->after, sim->last_committed) > 0) {',
  'if (0) {',
  'request->after, sim->last_committed) > 0'),

 ('m27_ack_regression', 'sim.c:1347 ACK must not go backwards', 'host/simulator/src/sim.c',
  'if (sim_cursor_compare(request->through, sim->ack_persisted) < 0) {',
  'if (0) {',
  'request->through, sim->ack_persisted) < 0'),

 ('m28_ack_upper', 'sim.c:1350 ACK must not exceed last_committed', 'host/simulator/src/sim.c',
  'if (sim_cursor_compare(request->through, sim->last_committed) > 0) {',
  'if (0) {',
  'request->through, sim->last_committed) > 0'),

 ('m29_resume_mode', 'sim.c:1412 RESUME_READY reports DEGRADED when continuity lost', 'host/simulator/src/sim.c',
  '    sim->device_mode = sim->continuity_ok ? LOCKER_WIRE_WIRE_DEVICE_MODE_READY\n                                          : LOCKER_WIRE_WIRE_DEVICE_MODE_DEGRADED;',
  '    sim->device_mode = LOCKER_WIRE_WIRE_DEVICE_MODE_READY;',
  'sim->continuity_ok ? LOCKER_WIRE_WIRE_DEVICE_MODE_READY'),

 ('m30_execute_horizon', 'sim.c:1679 an already expired not_after is refused', 'host/simulator/src/sim.c',
  '    if (request->not_after_mcu_ms <= now_ms ||\n        request->not_after_mcu_ms - now_ms > SIM_NOT_AFTER_HORIZON_MS ||\n        UINT64_MAX - now_ms < SIM_PREPARE_DEADLINE_MS) {',
  '    if ((request->not_after_mcu_ms <= now_ms && 0) ||\n        request->not_after_mcu_ms - now_ms > SIM_NOT_AFTER_HORIZON_MS ||\n        UINT64_MAX - now_ms < SIM_PREPARE_DEADLINE_MS) {',
  'request->not_after_mcu_ms <= now_ms'),

 ('m31_execute_replay', 'sim.c:1687 a reused action_no is REPLAY_BLOCKED, a jump is ACTION_GAP', 'host/simulator/src/sim.c',
  '        return request->action_no <= sim->action_watermark ? LOCKER_WIRE_RESULT_REPLAY_BLOCKED',
  '        return request->action_no < sim->action_watermark ? LOCKER_WIRE_RESULT_REPLAY_BLOCKED',
  'request->action_no <= sim->action_watermark ? LOCKER_WIRE_RESULT_REPLAY_BLOCKED'),

 ('m32_execute_journal', 'sim.c:1690 journal fault refuses new actions', 'host/simulator/src/sim.c',
  '    if (sim->fail_journal || sim->journal_state != LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY) {',
  '    if (0 && (sim->fail_journal || sim->journal_state != LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY)) {',
  'sim->fail_journal || sim->journal_state != LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY'),

 ('m33_execute_door', 'sim.c:1696 execute requires a closed door', 'host/simulator/src/sim.c',
  '    if (sim->door != LOCKER_WIRE_DOOR_STATE_CLOSED ||',
  '    if ((sim->door != LOCKER_WIRE_DOOR_STATE_CLOSED && 0) ||',
  'sim->door != LOCKER_WIRE_DOOR_STATE_CLOSED ||'),

 ('m34_borrow_item', 'sim.c:1698 BORROW requires item present', 'host/simulator/src/sim.c',
  '         sim->item != LOCKER_WIRE_ITEM_STATE_PRESENT) ||',
  '         (sim->item != LOCKER_WIRE_ITEM_STATE_PRESENT && 0)) ||',
  'LOCKER_WIRE_ITEM_STATE_PRESENT) ||'),

 ('m35_health_timeout', 'sim.c:1374 the 5s host-health timeout stops new actions', 'host/simulator/src/sim.c',
  '           sim->core_observed_age_ms <= SIM_HEALTH_TIMEOUT_MS && now_ms >= sim->last_health_ms &&\n           now_ms - sim->last_health_ms <= SIM_HEALTH_TIMEOUT_MS - sim->core_observed_age_ms;',
  '           sim->core_observed_age_ms <= SIM_HEALTH_TIMEOUT_MS && now_ms >= sim->last_health_ms;',
  'now_ms - sim->last_health_ms <= SIM_HEALTH_TIMEOUT_MS'),

 ('m36_pulse_flag', 'sim.c:1771 PULSE_STARTED sets pulse_active', 'host/simulator/src/sim.c',
  'sim->pulse_active = true;',
  'sim->pulse_active = false;',
  'sim->pulse_active = true'),

 ('m37_release_pulse', 'sim.c:1878 release refused while a pulse is active', 'host/simulator/src/sim.c',
  '    if (sim->pulse_active) {\n        return LOCKER_WIRE_RESULT_BAD_PRECONDITION;\n    }',
  '    if (0) {\n        return LOCKER_WIRE_RESULT_BAD_PRECONDITION;\n    }',
  'if (sim->pulse_active) {'),

 ('m38_release_idempotent', 'sim.c:1885 release without context is idempotent', 'host/simulator/src/sim.c',
  '    if (!sim->has_active) {\n        /* uart-v1.md:400-402: no active context and safe conditions -> idempotent. */',
  '    if (0) {\n        /* uart-v1.md:400-402: no active context and safe conditions -> idempotent. */',
  'no active context and safe conditions'),

 ('m39_dispatch_decode', 'sim.c:1934 decode failure keeps its own wire result', 'host/simulator/src/sim.c',
  'if (decode_result != LOCKER_WIRE_RESULT_OK) {',
  'if (0) {',
  'decode_result != LOCKER_WIRE_RESULT_OK'),

 ('m40_dispatch_session', 'sim.c:1940 non-HELLO request needs current session', 'host/simulator/src/sim.c',
  'if (session_scoped && !sim_session_matches(sim, &request->hello.session_id)) {',
  'if (0) {',
  'session_scoped && !sim_session_matches'),

 ('m41_continuity_flag', 'sim.c:945 HISTORY_INCOMPLETE latch flag', 'host/simulator/src/sim.c',
  '    if (!sim->continuity_ok) {\n        flags |= LOCKER_WIRE_FLAG_HISTORY_INCOMPLETE;\n    }',
  '    if (0) {\n        flags |= LOCKER_WIRE_FLAG_HISTORY_INCOMPLETE;\n    }',
  'flags |= LOCKER_WIRE_FLAG_HISTORY_INCOMPLETE'),

 ('m42_inject_latch', 'sim.c:2488 inject_gap latches continuity_ok=false', 'host/simulator/src/sim.c',
  '    sim->dropped_events += dropped;\n    sim->continuity_ok = false;',
  '    sim->dropped_events += dropped;\n    sim->continuity_ok = true;',
  'sim->dropped_events += dropped'),

 ('m43_boot_increment', 'sim.c:2179 every open increments boot_id', 'host/simulator/src/sim.c',
  'sim->boot_id += 1u;',
  'sim->boot_id += 0u;',
  'sim->boot_id += 1u'),

 ('m44_socket_mode', 'sim_main.c:302 sockets are chmod 0600', 'host/simulator/src/sim_main.c',
  'if (chmod(path, S_IRUSR | S_IWUSR) != 0) {',
  'if (chmod(path, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH) != 0) {',
  'chmod(path, S_IRUSR | S_IWUSR)'),

 ('m45_status_token', 'sim_main.c:475 refusals name their status', 'host/simulator/src/sim_main.c',
  '    case LOCKER_STATUS_INVALID:\n        return "invalid";',
  '    case LOCKER_STATUS_INVALID:\n        return "failed";',
  'return "invalid";'),

 ('m46_drop_link', 'sim_main.c:704 drop_link closes the accepted uart socket', 'host/simulator/src/sim_main.c',
  '        service_drop_uart(service);\n        control_reply(fd, true, NULL);',
  '        control_reply(fd, true, NULL);',
  'control_reply(fd, true, NULL);'),

 ('m47_frame_resync', 'sim_main.c:429 the daemon reconstitutes request_no little-endian', 'host/simulator/src/sim_main.c',
  '        raw[6u + index] = (uint8_t)(frame->request_no >> (index * 8u));',
  '        raw[6u + index] = (uint8_t)(frame->request_no >> ((3u - index) * 8u));',
  'raw[6u + index] = (uint8_t)(frame->request_no >>'),

 ('m48_event_flush', 'sim_main.c:400 async events are flushed after every reply', 'host/simulator/src/sim_main.c',
  '    while ((status = locker_sim_pop_event(service->sim, event, sizeof(event), &length)) ==\n           LOCKER_STATUS_OK) {',
  '    while ((status = locker_sim_pop_event(service->sim, event, sizeof(event), &length)) ==\n           LOCKER_STATUS_OK && 0) {',
  'locker_sim_pop_event(service->sim, event'),

 # R5-B-1: put connect() back into blocking mode. A listener whose accept queue
 # is full then parks the caller inside the kernel, so the test suite does not
 # fail an assertion -- it stops making progress and ctest has to kill it. That
 # is still a capture (the defect is observable) but it is a different shape
 # from a red assertion, so the harness reports it as CAUGHT_TIMEOUT.
 ('m49_connect_nonblock', 'transport.c:109 connect() must not outlive its deadline', 'host/device/src/transport.c',
  '    flags = fcntl(fd, F_GETFL, 0);\n    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {\n        (void)close(fd);\n        return LOCKER_STATUS_IO_ERROR;\n    }',
  '    flags = fcntl(fd, F_GETFL, 0);\n    (void)flags;',
  'flags | O_NONBLOCK'),

  # R6-B-1: discard the snprintf result again, so a 100+ byte control path
  # silently truncates its default journal name (107 bytes truncates back onto
  # the control socket itself).
  ('m50_journal_default_len', 'sim_main.c:910 the default journal path must fit, not truncate',
   'host/simulator/src/sim_main.c',
   '        if (written < 0 || (size_t)written >= sizeof(config.journal_path)) {\n'
   '            (void)fprintf(stderr, "journal path too long\\n");\n'
   '            return 2;\n        }',
   '        (void)written;',
   '(size_t)written >= sizeof(config.journal_path)'),

  # R6-B-1 / R7-B-1 / R7-B-2: the literal collision guard became a comparison of
  # resolved entries. These four points restore each half of the old behaviour:
  # the whole check dropped, the resolution step degraded to a raw string, and
  # the -wal / -shm half dropped.
  ('m51_journal_collision', 'sim_main.c:924 the roles may not share one entry',
   'host/simulator/src/sim_main.c',
   '    if (!paths_are_distinct(uart_path, control_path, config.journal_path)) {',
   '    if (false && !paths_are_distinct(uart_path, control_path, config.journal_path)) {',
   'paths_are_distinct(uart_path, control_path, config.journal_path)'),

  ('m52_canonical_entry_raw', 'sim_main.c:110 a missing entry resolves through its parent',
   'host/simulator/src/sim_main.c',
   '    written = snprintf(out, out_size, "%s/%s", parent_real, name);',
   '    written = snprintf(out, out_size, "%s", path);',
   '(void)parent_real;\n    (void)name;'),

  ('m53_wal_shm_off', 'sim_main.c:267 a socket may not sit on the journal -wal/-shm',
   'host/simulator/src/sim_main.c',
   '            if (clashes_with_sockets(sidecar, uart, control)) {',
   '            if (false && clashes_with_sockets(sidecar, uart, control)) {',
   'clashes_with_sockets(sidecar, uart, control)'),

  # R8-B-1: the identity guard now follows a final dangling link and also keeps
  # the link's own spelling. The spelling half is this module's own conservative
  # rule, not a SQLite requirement. These three points restore the round-7
  # behaviour one half at a time.
  ('m54_dangling_link_ignored', 'sim_main.c:205 a dangling final link is followed to its target',
   'host/simulator/src/sim_main.c',
   '        if (!read_link_target(current, next, sizeof(next))) {\n            return false;\n        }\n        memcpy(current, next, strlen(next) + 1u);',
   '        if (!read_link_target(current, next, sizeof(next))) {\n            return false;\n        }\n        return canonical_missing(current, out, out_size);\n        memcpy(current, next, strlen(next) + 1u);',
   'read_link_target(current, next, sizeof(next))'),

  ('m55_link_spelling_sidecars', 'sim_main.c:257 the sidecars are checked under both spellings',
   'host/simulator/src/sim_main.c',
   '    bases[1] = journal_spelling;',
   '    bases[1] = journal;',
   'bases[1] = journal_spelling;'),

  ('m56_link_hop_limit', 'sim_main.c:202 an unresolvable link chain is refused',
   'host/simulator/src/sim_main.c',
   '        if (hops > SIM_MAIN_LINK_HOPS) {\n            return false;\n        }',
   '        if (hops > SIM_MAIN_LINK_HOPS && 0) {\n            return false;\n        }',
   'hops > SIM_MAIN_LINK_HOPS'),
]

log = []
def w(s):
    log.append(s)
    print(s, flush=True)

w('# S1-03 mutation batch')
w('# source tree %s' % SRC)
w('# mode %s' % ('CHECK_ONLY' if CHECK_ONLY else 'FULL'))
for p in ['CMakeLists.txt', 'host/device/src/wire.c', 'host/device/src/codec.c', 'host/device/src/port.c',
          'host/device/src/transport.c', 'host/simulator/src/sim.c', 'host/simulator/src/sim_main.c']:
    w('# SOURCE_FINGERPRINT %s  %s  %d B' % (sha(os.path.join(SRC, p)), p, os.path.getsize(os.path.join(SRC, p))))

def prepare(dst):
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    os.makedirs(dst)
    for item in ['CMakeLists.txt', 'host', 'tests', 'contracts']:
        s = os.path.join(SRC, item)
        d = os.path.join(dst, item)
        if os.path.isdir(s):
            sh('cp -a "%s" "%s"' % (s, d))
        else:
            shutil.copy2(s, d)

# ---- check pass: every pattern must appear exactly once in the pristine tree
bad = 0
for name, ref, rel, old, new, needle in POINTS:
    path = os.path.join(SRC, rel)
    data = open(path, 'r', encoding='utf-8').read()
    cnt = data.count(old)
    ln = data.count('\n', 0, data.index(old)) + 1 if cnt == 1 else -1
    # R9-N-3: the ref label is part of the contract with the audit trail, so the
    # check pass verifies it too instead of only printing the live anchor.
    claimed = int(re.search(r':(\d+)', ref).group(1))
    if cnt != 1:
        bad += 1
        w('CHECK_FAIL %-22s count=%d %s' % (name, cnt, rel))
    elif claimed != ln:
        bad += 1
        w('CHECK_REF_MISMATCH %-22s ref=%d actual=%d %s' % (name, claimed, ln, rel))
    else:
        w('CHECK_OK   %-22s %s:%d' % (name, rel, ln))
w('# CHECK_SUMMARY points=%d bad=%d' % (len(POINTS), bad))
if CHECK_ONLY:
    w('# check only, no build')
    sys.exit(0 if bad == 0 else 1)

# ---- full pass
summary = []
for name, ref, rel, old, new, needle in POINTS:
    dst = os.path.join(BASE, 'src', name)
    prepare(dst)
    path = os.path.join(dst, rel)
    data = open(path, 'r', encoding='utf-8').read()
    index = data.index(old)
    anchor = '%s:%d' % (rel, data.count('\n', 0, index) + 1)
    tmp = path + '.tmp'
    open(tmp, 'w', encoding='utf-8', newline='').write(data.replace(old, new, 1))
    os.replace(tmp, path)
    bdir = os.path.join(BASE, 'build', name)
    if os.path.isdir(bdir):
        shutil.rmtree(bdir)
    os.makedirs(bdir)
    c = sh('cmake -S "%s" -B "%s" -DCMAKE_BUILD_TYPE=Debug -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON >/dev/null 2>&1' % (dst, bdir))
    rc_conf = c.returncode
    b = sh('make -j4 >/dev/null 2>&1', cwd=bdir)
    rc_build = b.returncode
    # R5-B-1: a mutation may hang the suite instead of failing an assertion
    # (a blocking connect() parks inside the kernel). ctest therefore runs under
    # an explicit wall-clock cap; rc 124 means "killed", which is a capture with
    # its own status rather than a silent NOT_CAUGHT or a fake red assertion.
    started_ctest = time.time()
    t = sh('timeout 300 ctest --output-on-failure 2>&1', cwd=bdir)
    ctest_seconds = time.time() - started_ctest
    rc_ctest = t.returncode
    out = t.stdout
    # R6-N-2: the filtered lines alone cannot prove "one test was killed at
    # 120.10 s", so the unfiltered CTest stdout is kept verbatim per point under
    # raw/<point>.log and the timeout evidence lines are copied into this log.
    raw_path = os.path.join(RAW_DIR, name + '.log')
    os.makedirs(RAW_DIR, exist_ok=True)
    open(raw_path, 'w', encoding='utf-8', newline='\n').write(
        'point=%s\nanchor=%s\ncommand=timeout 300 ctest --output-on-failure\n'
        'exit=%d\nseconds=%.2f\n---- ctest stdout ----\n%s\n'
        % (name, ref, rc_ctest, ctest_seconds, out))
    timeout_lines = [ln.strip() for ln in out.splitlines()
                     if '***Timeout' in ln or '***Failed' in ln or 'Timeout' in ln]
    # A mutation can hang the suite instead of failing an assertion: a blocking
    # connect() parks inside the kernel and no check ever reports red. Two
    # different kill shapes have to be recognised here, because ctest's own
    # per-test TIMEOUT property (tests/contract/device/CMakeLists.txt sets 120 s)
    # fires long before the outer "timeout 300" wrapper does:
    #   - ctest prints "***Timeout <n> sec" and exits 8 (its own property fired);
    #   - the outer wrapper exits 124 (ctest itself had to be killed).
    # Both mean "the suite stopped making progress", which is a capture with its
    # own status -- never a silent NOT_CAUGHT and never a fake red assertion.
    hung = rc_ctest == 124 or ('***Timeout' in out)
    # R2-N-3: keep two different numbers apart. An assertion line is emitted by
    # test_support.h as "FAIL <file>:<line>: <expression>"; a per-binary summary
    # line is "FAIL <test name> (<n> of <m> checks failed)". Only the first kind
    # is an assertion failure, and the denominator is summed over every red
    # binary instead of being taken from the first one.
    assertion_re = re.compile(r'^FAIL [^:()]+:\d+: ')
    summary_re = re.compile(r'^FAIL (\S+) \((\d+) of (\d+) checks failed\)$', re.M)
    # The Python daemon acceptance case prints "FAIL <label>" with neither a
    # file:line prefix nor an "(n of m)" summary. Those are daemon-level checks,
    # not C assertions, so they are counted separately instead of being dropped
    # (a CAUGHT daemon point that printed fails=0 looked like a non-capture).
    daemon_re = re.compile(r'^FAIL (?!\S+:\d+: )(?!\S+ \(\d+ of \d+ checks failed\)$)(.+)$')
    fails = [ln.strip() for ln in out.splitlines() if assertion_re.match(ln.strip())]
    daemon_fails = [m.group(1).strip() for m in (daemon_re.match(ln.strip()) for ln in out.splitlines()) if m]
    denoms = summary_re.findall(out)
    n = len(fails)
    dn = len(daemon_fails)
    d = sum(int(x[2]) for x in denoms)
    w('anchor=%s' % anchor)
    w('audit_ref=%s' % ref)
    w('mutation=%s -> %s' % (old.replace('\n', '\\n'), new.replace('\n', '\\n')))
    w('patch=OK configure=%d build=%d ctest=%d ctest_seconds=%.2f raw=raw/%s.log'
      % (rc_conf, rc_build, rc_ctest, ctest_seconds, name))
    for line in timeout_lines:
        w('  ctest_evidence ' + line)
    # R4-N-3: n counts every "FAIL <file>:<line>:" line, including binaries
    # whose own "(n of m)" summary line never made it into the captured output.
    # len(denoms)/d only cover the binaries that did print a complete summary, so
    # they must not be presented as the denominator of n.
    w('assertion_failures=%d assertion lines red; complete_summaries=%d binaries; '
      'denominator=%d checks in those summaries only'
      % (n, len(denoms), d))
    for name_s, num_s, den_s in denoms:
        w('  red_test %s %s of %s checks failed' % (name_s, num_s, den_s))
    if dn:
        w('daemon_checks_red=%d' % dn)
        for label in daemon_fails:
            w('  red_daemon_check ' + label)
    for l in fails:
        w('  ' + l)
    # B-15: a point whose build fails did not prove anything about the tests.
    # It is reported as BUILD_FAILED, never as CAUGHT, and the old binary is
    # never reused: bdir was created empty above and rmtree'd below.
    # A point can be BOTH red and hung: m43 makes one binary fail hundreds of
    # assertions while another binary blows the per-test TIMEOUT. Red evidence is
    # stronger than a kill, so it decides the status; the kill is still recorded
    # in the hung column so the two shapes stay distinguishable.
    red = (n > 0) or (dn > 0)
    if rc_conf != 0:
        status = 'CONFIGURE_FAILED'
    elif rc_build != 0:
        status = 'BUILD_FAILED'
    elif rc_ctest != 0 and red:
        status = 'CAUGHT'
    elif hung:
        status = 'CAUGHT_TIMEOUT'
    elif rc_ctest != 0:
        status = 'CAUGHT'
    else:
        status = 'NOT_CAUGHT'
    summary.append((name, status, rc_ctest, n, d, rc_build, dn, 1 if hung else 0))
    shutil.rmtree(bdir, ignore_errors=True)

w('# batch end')
w('# summary')
for s in summary:
    w('# %-24s %-10s ctest=%s fails=%s denom=%s build=%s daemon_red=%s hung=%s' % s)
caught = len([s for s in summary if s[1] == 'CAUGHT'])
timed_out = len([s for s in summary if s[1] == 'CAUGHT_TIMEOUT'])
not_caught = len([s for s in summary if s[1] == 'NOT_CAUGHT'])
build_failed = len([s for s in summary if s[1] in ('BUILD_FAILED', 'CONFIGURE_FAILED')])
w('# CAUGHT %d / %d' % (caught, len(summary)))
# R5-B-1: a hung suite is a capture too, counted apart so the two shapes stay
# distinguishable in the log.
w('# CAUGHT_TIMEOUT %d' % timed_out)
w('# CAUGHT_TOTAL %d / %d' % (caught + timed_out, len(summary)))
w('# NOT_CAUGHT %d' % not_caught)
w('# BUILD_FAILED %d' % build_failed)
w('# RAW_CTEST_STDOUT %s' % RAW_DIR)
os.makedirs(LOGDIR, exist_ok=True)
open(OUT, 'w', encoding='utf-8', newline='\n').write('\n'.join(log) + '\n')
open(os.path.join(LOGDIR, 'mutation.log'), 'w', encoding='utf-8', newline='\n').write('\n'.join(log) + '\n')
print('WROTE', OUT, os.path.join(LOGDIR, 'mutation.log'))
if build_failed:
    print('BUILD_FAILED_POINTS %d' % build_failed)
    sys.exit(3)
