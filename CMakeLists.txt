cmake_minimum_required(VERSION 3.0.2)
project(dynamic_gazebo_models)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")

## Find catkin macros and libraries
## if COMPONENTS list like find_package(catkin REQUIRED COMPONENTS xyz)
## is used, also find other catkin packages
include(FindProtobuf)
find_package(catkin REQUIRED COMPONENTS roscpp nodelet tf gazebo_plugins gazebo_ros message_generation geometry_msgs)
find_package(Boost COMPONENTS program_options REQUIRED)
find_package(Protobuf REQUIRED)

#add services:
add_service_files(DIRECTORY srv FILES AddGroup.srv DeleteGroup.srv OpenCloseDoors.srv SetVelDoors.srv TargetFloorElev.srv SetElevProps.srv OpenCloseElevDoors.srv ListGroups.srv)
add_message_files(DIRECTORY msg FILES ControlGroup.msg)

generate_messages(DEPENDENCIES std_msgs geometry_msgs)

#add catkin sourced packages:
catkin_package(
   LIBRARIES ${PROJECT_NAME}
   CATKIN_DEPENDS roscpp nodelet std_msgs geometry_msgs tf gazebo_plugins gazebo_ros message_runtime
)

#find and add gazebo
include (FindPkgConfig)
if (PKG_CONFIG_FOUND)
  pkg_check_modules(GAZEBO gazebo)
endif()

include_directories(
  ${GAZEBO_INCLUDE_DIRS} 
  ${catkin_INCLUDE_DIRS} 
  ${Boost_INCLUDE_DIR} 
  ${PROTOBUF_INCLUDE_DIR}
  /usr/include/
  ) 

#Controller Executables:
add_executable(dynamics_manager src/controllers/dynamics_manager.cpp src/controllers/control_group.h)
add_dependencies(dynamics_manager ${PROJECT_NAME}_generate_messages_cpp)
target_link_libraries(dynamics_manager ${GAZEBO_LIBRARIES} ${catkin_LIBRARIES} ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${PROTOBUF_LIBRARY})

add_executable(keyboard_op src/controllers/keyboard_op.cpp)
add_dependencies(keyboard_op ${PROJECT_NAME}_generate_messages_cpp)
target_link_libraries(keyboard_op ${catkin_LIBRARIES} ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY})

#Plugin Libraries:
add_library(door_plugin src/plugins/door_plugin.cc)
target_link_libraries(door_plugin ${GAZEBO_LIBRARIES} ${catkin_LIBRARIES} ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${PROTOBUF_LIBRARY})

add_library(elevator src/plugins/elevator_plugin.cc)
target_link_libraries(elevator ${GAZEBO_LIBRARIES} ${catkin_LIBRARIES} ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${PROTOBUF_LIBRARY})

add_library(auto_door src/plugins/auto_elev_door_plugin.cc)
target_link_libraries(auto_door ${GAZEBO_LIBRARIES} ${catkin_LIBRARIES} ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${PROTOBUF_LIBRARY})

install(TARGETS dynamics_manager keyboard_op door_plugin elevator auto_door
  ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION})

