// Copyright (c) 2014 Mohit Shridhar, David Lee

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <boost/bind.hpp>
#include <gazebo-11/gazebo/gazebo.hh>
#include <gazebo-11/gazebo/physics/physics.hh>
#include <gazebo-11/gazebo/physics/Model.hh>
#include <gazebo-11/gazebo/physics/World.hh>
#include <gazebo-11/gazebo/physics/Link.hh>
#include <gazebo-11/gazebo/common/common.hh>
#include <stdio.h>

#include <ros/ros.h>
#include <std_msgs/UInt32MultiArray.h>
#include <std_msgs/Float32MultiArray.h>
#include <std_msgs/UInt32.h>
#include <std_msgs/UInt8.h>
#include <std_msgs/Int32.h>

#include <ignition/math6/gz/math.hh>
#include <ignition/math6/gz/math/Vector3.hh>
#include <ignition/math6/gz/math/Pose3.hh>

#define DEFAULT_SLIDE_DISTANCE 0.711305
#define DEFAULT_SLIDE_SPEED 1 // in m/s

#define HEIGHT_LEVEL_TOLERANCE 1.5

#define ELEV_DOOR_STATE_OPEN 1
#define ELEV_DOOR_STATE_CLOSE 0
#define ELEV_DOOR_STATE_FREE 2

/*

Limitations:
	The door must be facing either the x axis or the y axis; not skewed in any sense

*/

enum DoorDirection
{
	LEFT,
	RIGHT
};

namespace gazebo
{
	class AutoElevDoorPlugin : public ModelPlugin
	{
	private:
		ros::NodeHandle *rosNode;
		event::ConnectionPtr updateConnection;
		ros::Subscriber target_floor_sub, est_floor_sub, open_close_sub, active_elevs_sub;

		gazebo::physics::ModelPtr model, elevatorModel;
		gazebo::physics::LinkPtr doorLink;

		std::string model_domain_space, elevator_ref_name, elevator_domain_space;
		int elevator_ref_num, targetFloor, estCurrFloor;
		DoorDirection direction;
		uint doorState;

		float openVel, closeVel, slide_speed;
		float max_trans_dist, maxPosX, maxPosY, minPosX, minPosY;
		bool isActive;

	public:
		AutoElevDoorPlugin()
		{
			std::string name = "auto_elevator_door_plugin";
			int argc = 0;
			ros::init(argc, NULL, name);
		}

		~AutoElevDoorPlugin()
		{
			delete rosNode;
		}

		void Load(gazebo::physics::ModelPtr _parent, sdf::ElementPtr _sdf)
		{
			determineDomainSpace(_sdf);
			determineCorresElev(_sdf);
			determineDoorDirection(_sdf);
			determineConstraints(_sdf);
			establishLinks(_parent);
			initVars();
		}

	private:
		void OnUpdate()
		{
			ros::spinOnce();
			activateDoors();
			checkSlideConstraints();
		}

		void determineDomainSpace(sdf::ElementPtr _sdf)
		{
			if (!_sdf->HasElement("model_domain_space"))
			{
				ROS_WARN("Model Domain Space not specified in the plugin reference. Defaulting to 'auto_door_'");
				model_domain_space = "auto_door_";
			}
			else
			{
				model_domain_space = _sdf->GetElement("model_domain_space")->Get<std::string>();
			}

			rosNode = new ros::NodeHandle("");

			if (!rosNode->hasParam("/model_dynamics_manager/elevator_domain_space"))
			{
				ROS_ERROR("The parameter 'elevator_domain_space' does not exist. Check that the elevator plugin sets this param");
				std::exit(EXIT_FAILURE);
			}
			else
			{
				rosNode->getParam("/model_dynamics_manager/elevator_domain_space", elevator_domain_space);
			}
		}

		void determineCorresElev(sdf::ElementPtr _sdf)
		{
			if (!_sdf->HasElement("elevator_name"))
			{
				ROS_ERROR("Elevator name not specified in the plugin reference. An auto door can exist only if there is a corresponding elevator.");
				std::exit(EXIT_FAILURE);
			}
			else
			{
				elevator_ref_name = _sdf->GetElement("elevator_name")->Get<std::string>();
			}
		}

		void determineDoorDirection(sdf::ElementPtr _sdf)
		{
			if (!_sdf->HasElement("door_direction"))
			{
				ROS_WARN("Door direction not specified in the plugin reference. Defaulting to 'left'");
				direction = LEFT;
			}
			else
			{
				std::string direction_str = _sdf->GetElement("door_direction")->Get<std::string>();
				direction = direction_str.compare("right") == 0 ? RIGHT : LEFT;
			}
		}

		void determineConstraints(sdf::ElementPtr _sdf)
		{
			if (!_sdf->HasElement("max_trans_dist"))
			{
				ROS_WARN("Maximum translation distance not specified in the plugin reference. Defaulting to '0.711305'");
				max_trans_dist = DEFAULT_SLIDE_DISTANCE;
			}
			else
			{
				max_trans_dist = _sdf->GetElement("max_trans_dist")->Get<float>();
			}

			if (!_sdf->HasElement("speed"))
			{
				ROS_WARN("Sliding speed not specified in the plugin reference. Defaulting to '1.0 m/s'");
				slide_speed = DEFAULT_SLIDE_SPEED;
			}
			else
			{
				slide_speed = _sdf->GetElement("speed")->Get<float>();
			}
		}

		void establishLinks(gazebo::physics::ModelPtr _parent)
		{
			model = _parent;
			doorLink = model->GetLink("door");

			target_floor_sub = rosNode->subscribe<std_msgs::Int32>("/elevator_controller/target_floor", 50, &AutoElevDoorPlugin::target_floor_cb, this);
			est_floor_sub = rosNode->subscribe<std_msgs::Int32>("/elevator_controller/" + elevator_ref_name + "/estimated_current_floor", 50, &AutoElevDoorPlugin::est_floor_cb, this);
			open_close_sub = rosNode->subscribe<std_msgs::UInt8>("/elevator_controller/door", 50, &AutoElevDoorPlugin::open_close_cb, this);
			active_elevs_sub = rosNode->subscribe<std_msgs::UInt32MultiArray>("/elevator_controller/active", 50, &AutoElevDoorPlugin::active_elevs_cb, this);

			updateConnection = event::Events::ConnectWorldUpdateBegin(boost::bind(&AutoElevDoorPlugin::OnUpdate, this));
		}

		void initVars()
		{
			// parse elevator reference number:
			std::string elevator_ref_num_str = elevator_ref_name;
			replaceSubstring(elevator_ref_num_str, elevator_domain_space, "");
			elevator_ref_num = atoi(elevator_ref_num_str.c_str());

			ROS_ASSERT(direction == LEFT || direction == RIGHT);

			// compute open-close velocities
			openVel = direction == RIGHT ? -slide_speed : slide_speed;
			closeVel = direction == RIGHT ? slide_speed : -slide_speed;

			// compute slide constraints
			float spawnPosX = model->WorldPose().Pos().X();
			minPosX = direction == RIGHT ? spawnPosX - max_trans_dist : spawnPosX;
			maxPosX = direction == RIGHT ? spawnPosX : spawnPosX + max_trans_dist;

			float spawnPosY = model->WorldPose().Pos().Y();
			minPosY = direction == RIGHT ? spawnPosY - max_trans_dist : spawnPosY;
			maxPosY = direction == RIGHT ? spawnPosY : spawnPosY + max_trans_dist;

			elevatorModel = model->GetWorld()->ModelByName(elevator_ref_name);
		}

		void activateDoors()
		{
			if (!isActive)
			{
				return;
			}

			float currElevHeight = elevatorModel->WorldPose().Pos().Z();
			float currDoorHeight = model->WorldPose().Pos().Z();
			float doorElevHeightDiff = fabs(currElevHeight - currDoorHeight);

			// Primary condition: the elevator is behind the doors
			if (doorElevHeightDiff > HEIGHT_LEVEL_TOLERANCE || estCurrFloor != targetFloor)
			{
				setDoorSlideVel(closeVel);
				return;
			}

			// Secondary condition: check if the door has to be forced open/closed [OVERIDE auto open-close]
			if (doorState == ELEV_DOOR_STATE_OPEN)
			{
				setDoorSlideVel(openVel);
				return;
			}
			else if (doorState == ELEV_DOOR_STATE_CLOSE)
			{
				setDoorSlideVel(closeVel);
				return;
			}

			// Else: open/close doors based on target floor reference
			setDoorSlideVel(openVel);
		}

		void setDoorSlideVel(float vel)
		{
			doorLink->SetLinearVel(ignition::math::Vector3(static_cast<double>(vel), static_cast<double>(vel), 0.0)); // we set the vel for both x & y directions since we don't know which direction the door is facing
		}

		void checkSlideConstraints()
		{
			float currDoorPosX = model->WorldPose().Pos().X();
			float currDoorPosY = model->WorldPose().Pos().Y();

			ignition::math::Pose3 constrainedPose(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);

			if (currDoorPosX > maxPosX)
			{
				constrainedPose.Pos().X() = maxPosX;
			}
			else if (currDoorPosX < minPosX)
			{
				constrainedPose.Pos().X() = minPosX;
			}
			else
			{
				constrainedPose.Pos().X() = currDoorPosX;
			}

			if (currDoorPosY > maxPosY)
			{
				constrainedPose.Pos().Y() = maxPosY;
			}
			else if (currDoorPosY < minPosY)
			{
				constrainedPose.Pos().Y() = minPosY;
			}
			else
			{
				constrainedPose.Pos().Y() = currDoorPosY;
			}

			constrainedPose.Pos().Z() = model->WorldPose().Pos().Z();
			constrainedPose.Rot().X() = model->WorldPose().Rot().X();
			constrainedPose.Rot().Y() = model->WorldPose().Rot().Y();
			constrainedPose.Rot().Z() = model->WorldPose().Rot().Z();

			model->SetWorldPose(constrainedPose);
		}

		void target_floor_cb(const std_msgs::Int32::ConstPtr &msg)
		{
			targetFloor = msg->data;
		}

		void est_floor_cb(const std_msgs::Int32::ConstPtr &msg)
		{
			estCurrFloor = msg->data;
		}

		void open_close_cb(const std_msgs::UInt8::ConstPtr &msg)
		{
			doorState = msg->data;
		}

		void active_elevs_cb(const std_msgs::UInt32MultiArray::ConstPtr &array)
		{
			isActive = false;

			for (std::vector<uint32_t>::const_iterator it = array->data.begin(); it != array->data.end(); ++it)
			{
				if (*it == elevator_ref_num)
				{
					isActive = true;
				}
			}
		}

		std::string replaceSubstring(std::string &s, std::string toReplace, std::string replaceWith)
		{
			return (s.replace(s.find(toReplace), toReplace.length(), replaceWith));
		}
	};

	GZ_REGISTER_MODEL_PLUGIN(AutoElevDoorPlugin);
}