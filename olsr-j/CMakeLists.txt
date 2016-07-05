INCLUDE_DIRECTORIES(${ROSS_SOURCE_DIR} ${ROSS_BINARY_DIR})

SET(olsr_srcs
	olsr-driver.cpp
	olsr.h
)

ADD_EXECUTABLE(olsr-j olsr-main.cpp ${olsr_srcs})

TARGET_LINK_LIBRARIES(olsr-j ROSS m)
