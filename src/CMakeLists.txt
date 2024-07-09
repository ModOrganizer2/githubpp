cmake_minimum_required(VERSION 3.16)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Network)

add_library(githubpp STATIC)

target_sources(githubpp
    PRIVATE
        github.cpp
	PUBLIC
		FILE_SET HEADERS
		BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}/../include
		FILES ${CMAKE_CURRENT_LIST_DIR}/../include/githubpp/github.h
)

target_link_libraries(githubpp PRIVATE Qt::Core Qt::Network)
set_target_properties(githubpp
    PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 20
)
target_include_directories(githubpp PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include/githubpp)

if (MSVC)
	target_compile_options(githubpp
		PRIVATE
		"/MP"
		"/Wall"
		"/external:anglebrackets"
		"/external:W0"
	)
	target_link_options(githubpp
		PRIVATE
		$<$<CONFIG:RelWithDebInfo>:/LTCG /INCREMENTAL:NO /OPT:REF /OPT:ICF>
	)

    set_target_properties(githubpp PROPERTIES
		VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_INSTALL_PREFIX}/bin
		VS_DEBUGGER_COMMAND ${CMAKE_INSTALL_PREFIX}/bin/ModOrganizer.exe
	    VS_STARTUP_PROJECT githubpp)
endif()

add_library(mo2::githubpp ALIAS githubpp)

# installation
install(TARGETS githubpp EXPORT githubppTargets FILE_SET HEADERS)
install(EXPORT githubppTargets
	FILE mo2-githubpp-targets.cmake
	NAMESPACE mo2::
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mo2-githubpp
)
