# no_access_files.cmake - the plug-in without the user's Access files must load, process and close without taking the
# host down: it stays an empty instance and says why (E345). rc7 of 0.8.41 dereferenced its null device object in
# process() there (found 2026-11-13). Linux only: HOME, XDG_CONFIG_HOME and WINEPREFIX are all its lookup uses.
#   cmake -DHOST=nativehost -DBUNDLE=<plug-in>.vst3 -DDIR=<scratch folder> -P no_access_files.cmake
get_filename_component(BUNDLE "${BUNDLE}" REALPATH) # the SDK loader needs the plain bundle path
file(REMOVE_RECURSE ${DIR})
file(MAKE_DIRECTORY ${DIR})
set(ENV{HOME} ${DIR})
set(ENV{XDG_CONFIG_HOME} ${DIR}/.config)
set(ENV{WINEPREFIX} ${DIR}/.wine)
unset(ENV{VTI_ACCESS_DIR})
# --notes: process() with events too, the path a DAW takes when a track plays into the empty plug-in
execute_process(COMMAND ${HOST} ${BUNDLE} --seconds 1 --notes RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "nativehost without Access files ended with ${rc}:\n${out}\n${err}")
endif()
if(NOT err MATCHES "Virus TI native: (no Access|cannot read)")
    message(FATAL_ERROR "the plug-in did not report the missing Access files:\n${err}")
endif()
message(STATUS "the plug-in without Access files: loaded, processed, closed")
