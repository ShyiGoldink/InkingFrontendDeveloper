# 一个"手写场景 + 生成按钮类"的示例。
#
# 它和 buttons_demo 是同一件事的两种写法：
#   buttons_demo：场景里有自检逻辑，组件也是手写的成员
#   scene_demo  ：场景缩到 5 行，把"一行构造整个界面"这个形态演示清楚
#
# 场景**不生成**（评估过：收益用手写就能拿到，代价是固定的，见 main.cpp 顶上那段）。
# 生成的只有**按钮类**——那才是"配置驱动"真正划算的地方（数据烘成字面量、
# 类型即名字、构造不查表）。

file(GLOB SCENE_DEMO_CONFIGS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/CXXCSS/Button/*.json")

# *.example.json 按约定不参与生成（CXXCSS.md §1）。
set(_scene_demo_sources "")
foreach(_config IN LISTS SCENE_DEMO_CONFIGS)
    if(NOT _config MATCHES "\\.example\\.json$")
        list(APPEND _scene_demo_sources "${_config}")
    endif()
endforeach()

if(NOT _scene_demo_sources)
    message(FATAL_ERROR
        "CXXCSS/Button/ 下没有可生成的配置：这个示例要的就是生成出来的按钮类，"
        "至少需要一个真实配置（例如 normalButton.json）。")
endif()

add_executable(scene_demo main.cpp)
target_link_libraries(scene_demo PRIVATE ink::core)
target_compile_features(scene_demo PRIVATE cxx_std_20)

ink_add_generated(scene_demo
    OUT_DIR   "${CMAKE_BINARY_DIR}"
    NAMESPACE "ink::cxxcss"
    HEADER    "inkgen/scene_demo/button_service.h"
    SOURCES   ${_scene_demo_sources})

if(INK_BUILD_TESTS)
    # 集成自检：跑的就是这个可执行文件（INK_AUTOQUIT 下它先断言"场景名 / 父级 /
    # 位置 / 命中 / 回调"，再开窗、再自动退），所以生成器或场景层任何一环断了，
    # 退出码都不是 0。
    add_test(NAME ink_scene_gen_test COMMAND scene_demo)
    set_tests_properties(ink_scene_gen_test PROPERTIES
        ENVIRONMENT "INK_AUTOQUIT=1")
endif()
