add_executable(logger_test logger_test.cc)
add_executable(lab1 lab1.cc)
add_executable(lab2 lab2.cc)





# 把所有target都收集到一起，并统一添加依赖
set(TARGETS_TO_LINK)
list(APPEND TARGETS_TO_LINK 
    logger_test
    lab1
    lab2
)

foreach(TARGET_NAME IN LISTS TARGETS_TO_LINK)
    target_link_libraries(${TARGET_NAME} PRIVATE Pnx pcap)
endforeach()