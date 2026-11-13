add_executable(unit_smoke smoke_test.cpp)
target_link_libraries(unit_smoke PRIVATE chroma_core)
chroma_set_warnings(unit_smoke)

add_test(NAME unit.smoke COMMAND $<TARGET_FILE:unit_smoke>)

# 色彩核心（CPU oracle）：三个测试文件分别覆盖语义、传递函数、矩阵。
add_executable(unit_color_math color_math_test.cpp)
target_link_libraries(unit_color_math PRIVATE chroma_color)
chroma_set_warnings(unit_color_math)

add_test(NAME unit.color_math COMMAND $<TARGET_FILE:unit_color_math>)

add_executable(unit_color_transfer color_transfer_test.cpp)
target_link_libraries(unit_color_transfer PRIVATE chroma_color)
chroma_set_warnings(unit_color_transfer)

add_test(NAME unit.color_transfer COMMAND $<TARGET_FILE:unit_color_transfer>)

add_executable(unit_color_matrix color_matrix_test.cpp)
target_link_libraries(unit_color_matrix PRIVATE chroma_color)
chroma_set_warnings(unit_color_matrix)

add_test(NAME unit.color_matrix COMMAND $<TARGET_FILE:unit_color_matrix>)
