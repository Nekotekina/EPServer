set(GIT_VERSION "unknown")

find_package(Git)

if(GIT_FOUND)
	set(EXIT_CODE 0)
	execute_process(COMMAND ${GIT_EXECUTABLE} describe --always
		WORKING_DIRECTORY ${SOURCE_DIR}
		RESULT_VARIABLE EXIT_CODE
		OUTPUT_VARIABLE GIT_VERSION)
	if(NOT ${exit_code} EQUAL 0)
		message(WARNING "git describe returned ${exit_code}, unable to include version.")
	endif()
	string(STRIP ${GIT_VERSION} GIT_VERSION)
else()
	message(WARNING "git not found, unable to include version.")
endif()

file(WRITE "${SOURCE_DIR}/git-version.inl" "#define GIT_VERSION \"${GIT_VERSION}\"\n")
