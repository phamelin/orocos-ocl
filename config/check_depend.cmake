# Modules path (for searching FindXXX.cmake files)
list(APPEND CMAKE_MODULE_PATH "${PROJ_SOURCE_DIR}/config")

include("CMakeDependentOption")

#
# Check for Doxygen and enable documentation building
#
find_package( Doxygen )
IF ( DOXYGEN_FOUND )
  MESSAGE( "Found Doxygen -- documentation can be built" )

ELSE ( DOXYGEN_FOUND )
  MESSAGE( "Doxygen not found -- unable to build documentation" )
ENDIF ( DOXYGEN_FOUND )

CMAKE_DEPENDENT_OPTION( DOC_GENERATE_API "Build API Documentation" OFF "DOXYGEN_FOUND" OFF )

#
# An option for tests, to make it easy to turn off all tests
#
OPTION( BUILD_TESTS "Turn me off to disable compilation of all tests" OFF )

###########################################################
#                                                         #
# Look for dependencies required by individual components #
#                                                         #
###########################################################

# Curse is not a readline dependency on win32 platform
if(NOT WIN32)
  find_package( Curses )

  IF ( CURSES_INCLUDE_DIR )
    MESSAGE("-- Looking for curses implementation - found libncurses")
    SET( CURSES 1 CACHE INTERNAL "libncurses" )
    SET( CURSES_IMPL ncurses)
  ELSE ( CURSES_INCLUDE_DIR )
    FIND_PATH( CURSES_INCLUDE_DIR termcap.h )
    IF ( CURSES_INCLUDE_DIR )
      MESSAGE("-- Looking for curses implementation - found termcap")
      FIND_LIBRARY(CURSES_LIBRARY termcap )
      SET( CURSES 1 CACHE INTERNAL "libncurses" )
      SET( CURSES_IMPL termcap)
    ELSE ( CURSES_INCLUDE_DIR  )
      MESSAGE("-- Looking for curses implementation - not found")
      SET( CURSES 0 CACHE INTERNAL "libncurses" )
    ENDIF ( CURSES_INCLUDE_DIR )
  ENDIF ( CURSES_INCLUDE_DIR )

endif()

FIND_PATH( READLINE_H readline/readline.h )
IF ( READLINE_H )
    MESSAGE("-- Looking for readline/readline.h - found")
    FIND_LIBRARY(READLINE_LIBRARY readline )
    SET( READLINE 1 CACHE INTERNAL "libreadline" )
    SET( READLINE_INCLUDE_DIR ${READLINE_H} )
ELSE ( READLINE_H  )
    MESSAGE("-- Looking for readline/readline.h - not found")
    SET( READLINE 0 CACHE INTERNAL "libreadline" )
ENDIF ( READLINE_H )

FIND_PATH( EDITLINE_H editline/readline.h )
IF ( EDITLINE_H )
    MESSAGE("-- Looking for editline/readline.h - found")
    FIND_LIBRARY(EDITLINE_LIBRARY edit )
    SET( EDITLINE 1 CACHE INTERNAL "libedit" )
ELSE ( EDITLINE_H  )
    MESSAGE("-- Looking for editline/readline.h - not found")
    SET( EDITLINE 0 CACHE INTERNAL "libedit" )
ENDIF ( EDITLINE_H )

# Since in ros-builds, log4cpp will be installed in log4cpp/install,
# we look there too.
if (ROS_ROOT)
  rosbuild_find_ros_package( log4cpp )
  set(LOG4CPP_ROOT ${log4cpp_PACKAGE_PATH}/install)
  message("ROS log4cpp in ${LOG4CPP_ROOT}")
endif(ROS_ROOT)
# Uses LOG4CPP_ROOT from above in ROS builds:
find_package( Log4cpp 6.0) # 6.0 is the Orocos extended API of log4cpp
if(LOG4CPP_FOUND)
  message("Found log4cpp in ${LOG4CPP_INCLUDE_DIRS}")
endif(LOG4CPP_FOUND)

find_package(Boost COMPONENTS program_options filesystem system)

# On win32, dynamically linking with boost_program_options requires
# BOOST_PROGRAM_OPTIONS_DYN_LINK. Otherwise, there is a linking error.
# See: https://svn.boost.org/trac/boost/ticket/2506
if(NOT Boost_USE_STATIC_LIBS)
    add_definitions(-DBOOST_PROGRAM_OPTIONS_DYN_LINK)
endif()

INCLUDE_DIRECTORIES( ${Boost_INCLUDE_DIR} ${READLINE_INCLUDE_DIR} ${CURSES_INCLUDE_DIR} )

