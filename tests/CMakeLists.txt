# 把所有target都收集到一起，并统一添加依赖
set(TARGETS_TO_LINK)
list(APPEND TARGETS_TO_LINK 
    logger_test
    ringbuffer_test
    lab1
    lab2
)

foreach(TARGET_NAME IN LISTS TARGETS_TO_LINK)
    add_executable(${TARGET_NAME} ${TARGET_NAME}.cc)
    target_link_libraries(${TARGET_NAME} PRIVATE Pnx pcap)
endforeach()