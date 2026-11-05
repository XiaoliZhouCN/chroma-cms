# 统一编译告警策略。/WX 暂不启用：Qt 头文件在 /W4 下仍有少量告警。

function(chroma_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /utf-8          # 必须：否则中文字符串字面量会报“文本后缀无效”
            /EHsc)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()