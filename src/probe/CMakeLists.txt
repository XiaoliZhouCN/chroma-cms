add_library(chroma_probe STATIC
    ProbeConfig.hpp
    ProbeConfig.cpp
    ProbeCsvWriter.hpp
    ProbeCsvWriter.cpp)

target_include_directories(chroma_probe PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_features(chroma_probe PUBLIC cxx_std_20)
target_link_libraries(chroma_probe PUBLIC Qt6::Core)
chroma_set_warnings(chroma_probe)
