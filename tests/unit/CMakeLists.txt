add_executable(unit_smoke smoke_test.cpp)
target_link_libraries(unit_smoke PRIVATE chroma_core)
chroma_set_warnings(unit_smoke)

add_test(NAME unit.smoke COMMAND $<TARGET_FILE:unit_smoke>)
