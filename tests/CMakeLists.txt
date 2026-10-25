set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 11)
if(NOT QT_DESIRED_VERSION)
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
    set(QT_DESIRED_VERSION ${QT_VERSION_MAJOR})
endif()
find_package(Qt${QT_DESIRED_VERSION} REQUIRED COMPONENTS Test Widgets Network)
set(CORE ${CMAKE_SOURCE_DIR}/QtScrcpy/QtScrcpyCore)
add_executable(qtscrcpy_tests test_features.cpp
    ../QtScrcpy/ui/wirelesspanel.cpp ../QtScrcpy/ui/wirelesspanel.h
    ../QtScrcpy/render/qyuvopenglwidget.cpp ../QtScrcpy/render/qyuvopenglwidget.h)
target_include_directories(qtscrcpy_tests PRIVATE
    ../QtScrcpy/ui ../QtScrcpy/util ../QtScrcpy/render
    ${CORE}/src/device/decoder ${CORE}/src/device/controller
    ${CORE}/src/device/controller/inputconvert ${CORE}/src/device/controller/inputconvert/keymap
    ${CORE}/src/device/android ${CORE}/src/common ${CORE}/src/third_party/ffmpeg/include)
target_link_libraries(qtscrcpy_tests PRIVATE QtScrcpyCore Qt${QT_DESIRED_VERSION}::Test
    Qt${QT_DESIRED_VERSION}::Widgets Qt${QT_DESIRED_VERSION}::Network)
if(QT_DESIRED_VERSION MATCHES 6)
    target_link_libraries(qtscrcpy_tests PRIVATE Qt6::OpenGLWidgets)
endif()
if(WIN32)
    target_link_libraries(qtscrcpy_tests PRIVATE opengl32)
endif()
set_target_properties(qtscrcpy_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/output/x64/${CMAKE_BUILD_TYPE}/$<0:>")
add_test(NAME features COMMAND qtscrcpy_tests -o test-results.xml,junitxml -o benchmark-results.xml,xml -o -,txt)
set_tests_properties(features PROPERTIES TIMEOUT 120)
