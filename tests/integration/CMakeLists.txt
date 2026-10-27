# S1-04 跨模块集成测试（docs/stages/stage-01-task-cards.md:67-75）。
#
# 这一层刻意把**真实**的四个模块串起来：
#   host/storage/repository.c  -> 真实 SQLite 仓储（临时目录里的真文件）
#   host/domain/*.c            -> 真实 Core（真实工作线程、真实完成槽）
#   host/device/port.c         -> 真实设备适配器（真实 reader 线程、真实 AF_UNIX）
#   host/simulator/sim.c       -> 真实设备侧状态机（独立 SQLite journal）
#
# 单测用端口替身，看不见「泵顺序」「线程归属」「ACK 丢失」这类跨模块事实；
# 这一层专门补这些中断位置（准备->发送->ACK->脉冲->落库），因此必须走
# locker_core_submit/wait_result 与真实 socket，不允许直接调内部 handler。
#
# 依赖只有 locker_simulator（它 PUBLIC 传递 locker_device 与 locker_common）
# 与 locker_storage、locker_domain；不链接 daemon（locker-device-sim）。

set(LOCKER_INTEGRATION_TESTS
    test_core_interrupt_matrix
)

foreach(test_name IN LISTS LOCKER_INTEGRATION_TESTS)
    add_executable(${test_name} ${test_name}.c)

    target_include_directories(${test_name}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/tests/unit/common
            ${CMAKE_SOURCE_DIR}/tests/unit/device
    )

    target_link_libraries(${test_name}
        PRIVATE
            locker_domain
            locker_storage
            locker_simulator
            Threads::Threads
    )

    set_target_properties(${test_name} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )

    locker_apply_warnings(${test_name})

    add_test(NAME integration.${test_name} COMMAND ${test_name})
    # 真实 socket + 真实 SQLite + 真实线程；超时只用来兜住挂死，不代表慢是正常。
    set_tests_properties(integration.${test_name} PROPERTIES TIMEOUT 180)
endforeach()
