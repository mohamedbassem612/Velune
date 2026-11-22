cmake_minimum_required(VERSION 3.21)
if(NOT FFMPEG)
 find_program(FFMPEG ffmpeg REQUIRED)
endif()
if(NOT OUTPUT_DIR)
 message(FATAL_ERROR "Pass -DOUTPUT_DIR=<fixture-directory>")
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
foreach(pair "mp3;libmp3lame" "m4a;aac" "opus;libopus" "flac;flac" "wav;pcm_s16le")
 list(GET pair 0 extension)
 list(GET pair 1 codec)
 execute_process(COMMAND "${FFMPEG}" -nostdin -hide_banner -loglevel error -y
  -f lavfi -i "sine=frequency=997:sample_rate=48000:duration=4" -ac 1 -c:a "${codec}"
  -fflags +bitexact -flags:a +bitexact -map_metadata -1 "${OUTPUT_DIR}/tone.${extension}"
  RESULT_VARIABLE result)
 if(NOT result EQUAL 0)
  message(FATAL_ERROR "Could not generate ${extension}")
 endif()
endforeach()
execute_process(COMMAND "${FFMPEG}" -nostdin -hide_banner -loglevel error -y
 -f lavfi -i "color=c=black:size=32x32:rate=5:duration=4" -i "${OUTPUT_DIR}/tone.m4a"
 -map 0:v -map 1:a -c:v mpeg4 -c:a copy -map_metadata -1 -shortest "${OUTPUT_DIR}/audio-video.mp4"
 COMMAND_ERROR_IS_FATAL ANY)
