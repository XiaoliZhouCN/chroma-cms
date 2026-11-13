# 开发工具（tools）
#
# 这里的工具不随 app 发布，只服务开发/测试：
#   doc-check/   目录文档规范检查器（ctest: docs.structure）
#
# 后续工具（chroma-probe / chroma-bench / pattern-gen）先在该目录下加源文件，
# 再在下面的 target 列表里注册。

add_executable(doc_check
    doc-check/doc_check.cpp)

target_compile_features(doc_check PRIVATE cxx_std_20)
chroma_set_warnings(doc_check)
