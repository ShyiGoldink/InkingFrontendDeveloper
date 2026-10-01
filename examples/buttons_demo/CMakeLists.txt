# 一个由 CXXCSS 配置驱动的示例：按钮的样子全部来自 CXXCSS/Button/*.json。
#
# 配置列表用 GLOB + CONFIGURE_DEPENDS 收：新增一个 json 之后，下次构建
# 会重新 glob 并重新生成，不需要手动重新配置 CMake。

file(GLOB BUTTONS_DEMO_CONFIGS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/CXXCSS/Button/*.json")

# *.example.json 按约定不参与生成（CXXCSS.md §1），在这里就滤掉，
# 免得它们在生成器里触发"你传了示例文件"的提示。
set(_buttons_demo_sources "")
foreach(_config IN LISTS BUTTONS_DEMO_CONFIGS)
    if(NOT _config MATCHES "\\.example\\.json$")
        list(APPEND _buttons_demo_sources "${_config}")
    endif()
endforeach()

if(NOT _buttons_demo_sources)
    message(FATAL_ERROR
        "CXXCSS/Button/ 下没有可生成的配置（只有 *.example.json）。\n"
        "这个示例就是拿配置驱动的，至少需要一个真实配置，例如 normalButton.json。")
endif()

add_executable(buttons_demo main.cpp)
target_link_libraries(buttons_demo PRIVATE ink::core)
target_compile_features(buttons_demo PRIVATE cxx_std_20)

# 生成 + 挂上去。OUT_DIR 是 include 根（会被加进 include 路径），
# HEADER 带一层 inkgen/ 子目录，所以调用方写 <inkgen/buttons_demo/button_service.h>。
ink_add_generated(buttons_demo
    OUT_DIR   "${CMAKE_BINARY_DIR}"
    NAMESPACE "ink::cxxcss"
    HEADER    "inkgen/buttons_demo/button_service.h"
    SOURCES   ${_buttons_demo_sources})

# 上面的函数把这两个路径传了回来（别在这里再抄一遍 HEADER——抄错就是
# 两个不同的产物，而且报错会指向"找不到头文件"，很难联想到是抄错了）。
set(INK_BUTTONS_DEMO_HEADER "${buttons_demo_GENERATED_HEADER}")
set(INK_BUTTONS_DEMO_INCLUDE_DIR "${buttons_demo_GENERATED_INCLUDE_DIR}")

if(INK_BUILD_TESTS)
    # 集成自检：跑的是**同一个可执行文件**（在 INK_AUTOQUIT 下它会先做一遍
    # "配置→库→渲染树→点击回调"的断言再开窗），所以这条链路上任何一环断了，
    # 它的退出码就不是 0。
    #
    # 注意这里**不能**写 COMMAND buttons_demo $<TARGET_FILE:buttons_demo>——
    # 那会把路径当成命令行参数传进去。
    add_test(NAME ink_button_gen_test COMMAND buttons_demo)
    set_tests_properties(ink_button_gen_test PROPERTIES
        ENVIRONMENT "INK_AUTOQUIT=1")

    # 生成代码的运行时自检：直接 include 生成出来的那份头，验证
    # "静态登记 → 库里查得到 → 按名字构造拿到配置"这一环。
    # 它链接 buttons_demo_generated 的对象文件，所以跑的就是这份产物。
    add_executable(ink_generated_test
        "${CMAKE_SOURCE_DIR}/tests/test_button_gen.cpp")
    target_link_libraries(ink_generated_test PRIVATE ink::core)
    target_sources(ink_generated_test PRIVATE
        $<TARGET_OBJECTS:buttons_demo_generated>)
    # 生成物的 include 根进 include 路径：生成代码与自检 include 的都是
    # `inkgen/buttons_demo/button_service.h`，相对的就是这个根。
    target_include_directories(ink_generated_test PRIVATE
        "${INK_BUTTONS_DEMO_INCLUDE_DIR}")
    target_compile_definitions(ink_generated_test PRIVATE
        INK_GENERATED_HEADER=<${INK_BUTTONS_DEMO_HEADER}>)
    target_compile_features(ink_generated_test PRIVATE cxx_std_20)
    add_test(NAME ink_generated_test COMMAND ink_generated_test)
endif()
