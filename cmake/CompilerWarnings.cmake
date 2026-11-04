# 统一编译告警策略（Docs/CONVENTIONS.md §3）。
#
# /WX 暂时不启用：Qt 6.11 的头文件在 /W4 下仍会产生少量告警，
# 引入第三方代码后需要先收敛再打开。见 Docs/SESSION_INDEX.md。

function(chroma_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /utf-8
            /EHsc)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()
