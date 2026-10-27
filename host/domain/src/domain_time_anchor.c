#define _POSIX_C_SOURCE 200809L

/*
 * S1-04 业务核心：主机 <-> MCU 单调时钟换算。
 *
 * docs/architecture/internal-interfaces.md:133 的规则：设主机发起时刻 h0、
 * 收到响应的时刻 h3、MCU 收到请求的单调时刻 m1、MCU 发出的 m2，则真实偏移
 * 一定落在 [m2 - h3, m1 - h0] 这个保守区间里。只有 RTT <= 200 ms、样本年龄
 * <= 3000 ms 且 boot 一致时才允许用它换算动作期限；否则一律 fail closed，
 * 不猜、不外推（EXECUTE 的 not_after_mcu_ms 直接不发）。
 *
 * 本文件不做任何阻塞 IO：request 只是登记一个在途请求并调用适配器。
 */

#include "domain_internal.h"

#include <string.h>

/* 保守区间：把往返误差整体吃进两侧边界，宁可窄一点也不要放宽期限。 */
static void anchor_apply_sample(locker_core_t *core, uint64_t h0, uint64_t h3, uint64_t m1, uint64_t m2)
{
    int64_t low = (int64_t)m2 - (int64_t)h3;
    int64_t high = (int64_t)m1 - (int64_t)h0;
    if (low > high) {
        /* 乱序样本（MCU 时钟被拉回或主机时钟回退）：不采纳。 */
        core->anchor_valid = false;
        return;
    }
    core->anchor_offset_low_ms = low;
    core->anchor_offset_high_ms = high;
    core->anchor_sampled_ms = h3;
    core->anchor_valid = true;
}

locker_status_t domain_anchor_request(locker_core_t *core)
{
    if (core == NULL || core->device == NULL || !core->device_session_up || !core->session.valid) {
        return LOCKER_STATUS_NOT_READY;
    }
    uint64_t now = domain_now(core);
    /* 同一时刻只允许一个在途锚：多余请求只会浪费 UART 带宽。 */
    for (uint32_t index = 0u; index < LOCKER_DOMAIN_PENDING_SLOTS; index += 1u) {
        if (core->pending[index].used && core->pending[index].purpose == DOMAIN_PURPOSE_ANCHOR) {
            return LOCKER_STATUS_BUSY;
        }
    }
    uint32_t request_no = domain_request_no(core, DOMAIN_PURPOSE_ANCHOR);
    if (request_no == 0u) {
        return LOCKER_STATUS_BUSY;
    }
    core->last_anchor_ms = now;
    locker_status_t status = locker_device_time_anchor(core->device, request_no);
    if (status != LOCKER_STATUS_OK) {
        domain_pending_clear(core, DOMAIN_PURPOSE_ANCHOR);
    }
    return status;
}

void domain_anchor_apply(locker_core_t *core, const locker_device_reply_t *reply)
{
    if (core == NULL || reply == NULL) {
        return;
    }
    const locker_wire_time_anchor_response_t *anchor = &reply->body.time_anchor;
    if (anchor->result != LOCKER_WIRE_RESULT_OK) {
        core->anchor_valid = false;
        return;
    }
    /* boot 不一致说明中间重启过：旧样本不能跨 boot 使用。 */
    if (!core->session.valid || anchor->boot_id != core->session.boot_id) {
        core->anchor_valid = false;
        return;
    }
    uint64_t h0 = reply->request_started_host_mono_ms;
    uint64_t h3 = reply->received_host_mono_ms;
    if (h3 < h0) {
        core->anchor_valid = false;
        return;
    }
    uint64_t rtt = h3 - h0;
    if (rtt > LOCKER_TIME_ANCHOR_MAX_RTT_MS) {
        core->anchor_valid = false;
        return;
    }
    if (anchor->mcu_send_mono_ms < anchor->mcu_receive_mono_ms) {
        core->anchor_valid = false;
        return;
    }
    anchor_apply_sample(core, h0, h3, anchor->mcu_receive_mono_ms, anchor->mcu_send_mono_ms);
}

locker_status_t domain_anchor_mcu_now(const locker_core_t *core, uint64_t *out_low, uint64_t *out_high)
{
    if (core == NULL || out_low == NULL || out_high == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (!core->anchor_valid) {
        return LOCKER_STATUS_NOT_READY;
    }
    uint64_t now = domain_now((locker_core_t *)core);
    if (now < core->anchor_sampled_ms) {
        /* 单调时钟回退：样本作废。 */
        return LOCKER_STATUS_NOT_READY;
    }
    uint64_t age = now - core->anchor_sampled_ms;
    if (age > LOCKER_TIME_ANCHOR_MAX_AGE_MS) {
        /* 样本太旧：换算误差已经不可界定，宁可拒绝动作。 */
        return LOCKER_STATUS_NOT_READY;
    }
    int64_t base = (int64_t)now;
    int64_t low = base + core->anchor_offset_low_ms;
    int64_t high = base + core->anchor_offset_high_ms;
    if (low < 0 || high < 0) {
        return LOCKER_STATUS_NOT_READY;
    }
    *out_low = (uint64_t)low;
    *out_high = (uint64_t)high;
    return LOCKER_STATUS_OK;
}
