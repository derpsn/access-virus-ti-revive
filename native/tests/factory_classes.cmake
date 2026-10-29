# factory_classes.cmake - the built plug-in's VST3 classes (moduleinfotool): its own class id and the original Access
# plug-in's, so hosts that ignore moduleinfo compatibility (Ableton Live 11, E221) open projects made with the original;
# and the Snow plug-in (the Snow's 4-part editor, E361).
#   cmake -DTOOL=moduleinfotool -DBUNDLE=<plug-in>.vst3 -DOUT=file.json -DOWN=<cid> -DORIGINAL=<cid> -DSNOW=<cid>
#         -P factory_classes.cmake
get_filename_component(BUNDLE "${BUNDLE}" REALPATH) # the SDK loader needs the plain bundle path
execute_process(COMMAND ${TOOL} -create -version 0.0.0 -path ${BUNDLE} -output ${OUT} RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "moduleinfotool failed (${rc})")
endif()
file(READ ${OUT} info)
foreach(cid ${OWN} ${ORIGINAL} ${SNOW})
    if(NOT info MATCHES "\"CID\": \"${cid}\"")
        message(FATAL_ERROR "class ${cid} missing:\n${info}")
    endif()
endforeach()
if(NOT info MATCHES "\"Name\": \"TI Revive Virus Control \\(Snow\\)\"")
    message(FATAL_ERROR "the Snow class is not named \"TI Revive Virus Control (Snow)\":\n${info}")
endif()
message(STATUS "classes ${OWN}, ${ORIGINAL} and ${SNOW} present")
