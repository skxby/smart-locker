# 非阻塞项

- 主机既有integration.test_core_interrupt_matrix首轮在acklost场景创建操作返回NOT_READY，复验全量36/36通过。本卡未修改主机代码，2872文件哈希一致。保留两轮日志，后续涉及主机时序时跟进，不以复验抹去偶发记录。
- 本卡只证明低电流负载稳态默认关闭。复位瞬态、GPIO高电平裕量及硬截止波形在S2-05验收；profile保持UNVERIFIED，禁止真锁动作。
