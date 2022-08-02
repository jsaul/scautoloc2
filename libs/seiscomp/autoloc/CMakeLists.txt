SET(LIBAUTOLOC_SOURCES
	associator.cpp
	autoloc.cpp
	config.cpp
	datamodel.cpp
	import.cpp
	locator.cpp
	nucleator.cpp
	objectqueue.cpp
	publication.cpp
	sc3adapters.cpp
	stationconfig.cpp
	stationlocationfile.cpp
	util.cpp
)

SET(LIBAUTOLOC_HEADERS
	associator.h
	autoloc.h
	datamodel.h
	locator.h
	nucleator.h
	objectqueue.h
	stationconfig.h
	stationlocationfile.h
#	util.h
)

IF(CMAKE_COMPILER_IS_GNUCC)
        SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Wall -Wextra")
ENDIF()

SC_ADD_LIBRARY(LIBAUTOLOC autoloc)
SC_LIB_INSTALL_HEADERS(LIBAUTOLOC)
