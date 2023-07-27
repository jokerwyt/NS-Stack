add_executable(logger_test logger_test.c)






# 把所有target都收集到一起，并统一添加依赖
set(TARGETS_TO_LINK)
list(APPEND TARGETS_TO_LINK logger_test)

foreach(TARGET_NAME IN LISTS TARGETS_TO_LINK)
    target_link_libraries(${TARGET_NAME} PRIVATE Pnx pcap)
endforeach()