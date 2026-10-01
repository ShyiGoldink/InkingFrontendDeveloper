# inkgen：CXXCSS 配置 → C++ 代码（构建期工具 + 生成规则的自检）。
# 只依赖标准库，不碰 SDL。

add_library(inkgen_lib STATIC
    tools/inkgen/Json.cpp
    tools/inkgen/Cxxcss.cpp
    tools/inkgen/Emit.cpp)

# 公开 include 目录给自检用：自检要直接调这张库，而不是 fork 一个进程。
target_include_directories(inkgen_lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tools/inkgen>)
# 生成器要产出运行期那份数据结构（ink::ButtonData），所以需要 include/ 根——
# 但**不链接 ink_core**：那是"SDF 与 SDL"的世界，构建期工具不该拖进来。
# ButtonData.h 只依赖标准库，所以这里只要一个 include 路径就够。
target_include_directories(inkgen_lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(inkgen_lib PUBLIC cxx_std_20)

# 生成器本体。
add_executable(inkgen tools/inkgen/main.cpp)
target_link_libraries(inkgen PRIVATE inkgen_lib)
target_compile_features(inkgen PRIVATE cxx_std_20)

# 本机依赖路径覆盖文件不存在时，也要能用：生成器一个第三方依赖都没有。
if(NOT INK_BUILD_TESTS)
    return()
endif()

add_executable(inkgen_test tests/test_inkgen.cpp)
target_link_libraries(inkgen_test PRIVATE inkgen_lib)
target_compile_features(inkgen_test PRIVATE cxx_std_20)
# 自检要按绝对路径找 fixture，不能靠"当前工作目录正好是源码树"——ctest
# 默认在构建目录里跑，相对路径一定找不到。
target_compile_definitions(inkgen_test PRIVATE
    INK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME inkgen_test COMMAND inkgen_test)

# 真实配置（CXXCSS/Button/*.json）也过一遍校验：配置写坏了，构建就该红，
# 而不是等运行期发现按钮样子不对。
# 没有非示例配置时允许为空——那时这个测试只验证"空输入不算错误"。
file(GLOB INK_CXXCSS_BUTTON_CONFIGS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/CXXCSS/Button/*.json")
set(_real_configs "")
foreach(_config IN LISTS INK_CXXCSS_BUTTON_CONFIGS)
    if(NOT _config MATCHES "\\.example\\.json$")
        list(APPEND _real_configs "${_config}")
    endif()
endforeach()

if(_real_configs)
    add_test(NAME inkgen_validate_cxxcss
        COMMAND inkgen --validate ${_real_configs})
endif()
