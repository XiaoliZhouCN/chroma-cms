add_subdirectory(unit)

# 集成冒烟：无交互启动窗口后自动退出，验证 Qt 运行时与原生窗口创建。
# 需要桌面会话（Windows 本机）；CI 上不跑，见 Docs/TESTING.md §6。
add_test(NAME app.selftest COMMAND $<TARGET_FILE:ChromaCMS> --selftest)
set_tests_properties(app.selftest PROPERTIES
    TIMEOUT 60
    LABELS "gui")
