add_executable(play_audio
    play_audio.cpp
)

target_link_libraries(play_audio
    PRIVATE
        opendtp
)

add_executable(record_audio
    record_audio.cpp
)

target_link_libraries(record_audio
    PRIVATE
        opendtp
)
