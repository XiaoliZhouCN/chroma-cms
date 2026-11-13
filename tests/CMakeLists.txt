add_subdirectory(unit)

# 目录文档规范门禁（AGENTS.md §12 / docs/decisions/0001）。
add_test(NAME docs.structure
         COMMAND $<TARGET_FILE:doc_check> "${PROJECT_SOURCE_DIR}")
