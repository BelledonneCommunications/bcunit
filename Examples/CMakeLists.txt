############################################################################
# CMakeLists.txt
# Copyright (C) 2014  Belledonne Communications, Grenoble France
#
############################################################################
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#
############################################################################

add_library(bcunitexamples STATIC ExampleTests.c)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

if(ENABLE_BCUNIT_BASIC)
	add_subdirectory(BasicTest)
endif()
if(ENABLE_BCUNIT_CONSOLE)
	add_subdirectory(ConsoleTest)
endif()
if(ENABLE_BCUNIT_CURSES)
	add_subdirectory(CursesTest)
endif()
if(ENABLE_BCUNIT_AUTOMATED)
	add_subdirectory(AutomatedTest)
endif()
