SET(EXTRA_LIBRARIES)
SET(EXTRA_DEPS)
OPTION( BUILD_RTALLOC "Build with rt-malloc support (requires rt-malloc in RTT to be enabled)." TRUE)

# RTALLOC is forced to off on win32
IF(WIN32)
  SET(BUILD_RTALLOC OFF CACHE BOOL "docstring" FORCE)
ENDIF()

IF ( BUILD_RTALLOC )
  # default to 20 kilobytes - see minimum size warning in deployer-funcs.cpp
  SET(ORO_DEFAULT_RTALLOC_SIZE 20480 CACHE STRING "Real-time memory pool size (bytes)")
  ADD_DEFINITIONS("-DORO_BUILD_RTALLOC -DORO_DEFAULT_RTALLOC_SIZE=${ORO_DEFAULT_RTALLOC_SIZE}")
ELSE ( BUILD_RTALLOC )
  ADD_DEFINITIONS("-DORO_DEFAULT_RTALLOC_SIZE=0")
ENDIF ( BUILD_RTALLOC )

IF ( BUILD_LOGGING )
  INCLUDE_DIRECTORIES( "${LOG4CPP_INCLUDE_DIRS}" )
  LINK_DIRECTORIES( "${LOG4CPP_LIBRARY_DIRS}" )
  LIST(APPEND EXTRA_LIBRARIES "${LOG4CPP_LIBRARY}")
  LIST(APPEND EXTRA_DEPS "orocos-ocl-log4cpp")
  ADD_DEFINITIONS("-DORO_BUILD_LOGGING")
ENDIF ( BUILD_LOGGING )

include_directories(${CMAKE_CURRENT_BINARY_DIR})
configure_file(installpath.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/installpath.hpp @ONLY)

# Local Deployer application
IF ( BUILD_TASKBROWSER AND BUILD_DEPLOYMENT)

    ADD_EXECUTABLE( deployer-${OROCOS_TARGET} deployer.cpp deployer-funcs.cpp )
    program_add_deps( deployer-${OROCOS_TARGET} ${EXTRA_DEPS} )
    program_add_deps( deployer-${OROCOS_TARGET} ${OROCOS-RTT_LIBRARIES} orocos-ocl-taskbrowser orocos-ocl-deployment ${Boost_PROGRAM_OPTIONS_LIBRARY} ${EXTRA_LIBRARIES})
    INSTALL( TARGETS deployer-${OROCOS_TARGET} RUNTIME DESTINATION bin)

ENDIF ( BUILD_TASKBROWSER AND BUILD_DEPLOYMENT)

# Remote TaskBrowser application
IF ( BUILD_TASKBROWSER AND OROCOS-RTT_CORBA_FOUND)

    ADD_EXECUTABLE( ctaskbrowser-${OROCOS_TARGET} ctaskbrowser.cpp )
    program_add_deps( ctaskbrowser-${OROCOS_TARGET} ${EXTRA_DEPS} )
    program_add_deps( ctaskbrowser-${OROCOS_TARGET} ${OROCOS-RTT_LIBRARIES} orocos-ocl-taskbrowser ${OROCOS-RTT_CORBA_LIBRARIES} ${EXTRA_LIBRARIES} )
    INSTALL( TARGETS ctaskbrowser-${OROCOS_TARGET} RUNTIME DESTINATION bin)

ENDIF ( BUILD_TASKBROWSER AND OROCOS-RTT_CORBA_FOUND)

# Deployment Server which can be accessed using ctaskbrowser
IF ( BUILD_DEPLOYMENT AND OROCOS-RTT_CORBA_FOUND)

    ADD_EXECUTABLE( cdeployer-${OROCOS_TARGET} cdeployer.cpp deployer-funcs.cpp )
    program_add_deps( cdeployer-${OROCOS_TARGET} ${EXTRA_DEPS} )
    program_add_deps( cdeployer-${OROCOS_TARGET} ${OROCOS-RTT_LIBRARIES} ${OROCOS-RTT_CORBA_LIBRARIES} orocos-ocl-deployment-corba ${Boost_PROGRAM_OPTIONS_LIBRARY} ${EXTRA_LIBRARIES})
    INSTALL( TARGETS cdeployer-${OROCOS_TARGET} RUNTIME DESTINATION bin)

    ADD_EXECUTABLE( deployer-corba-${OROCOS_TARGET} deployer-corba.cpp deployer-funcs.cpp )
    program_add_deps( deployer-corba-${OROCOS_TARGET} ${EXTRA_DEPS} )
    program_add_deps( deployer-corba-${OROCOS_TARGET} ${OROCOS-RTT_LIBRARIES} ${OROCOS-RTT_CORBA_LIBRARIES} orocos-ocl-deployment-corba orocos-ocl-taskbrowser ${Boost_PROGRAM_OPTIONS_LIBRARY} ${EXTRA_LIBRARIES})
    INSTALL( TARGETS deployer-corba-${OROCOS_TARGET} RUNTIME DESTINATION bin)

ENDIF ( BUILD_DEPLOYMENT AND OROCOS-RTT_CORBA_FOUND)

