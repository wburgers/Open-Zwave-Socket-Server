//-----------------------------------------------------------------------------
//
//	Main.cpp
//
//	Minimal application to test OpenZWave.
//
//	Creates an OpenZWave::Driver and the waits.  In Debug builds
//	you should see verbose logging to the console, which will
//	indicate that communications with the Z-Wave network are working.
//
//	Copyright (c) 2010 Mal Lansell <mal@openzwave.com>
//
//
//	SOFTWARE NOTICE AND LICENSE
//
//	This file is part of OpenZWave.
//
//	OpenZWave is free software: you can redistribute it and/or modify
//	it under the terms of the GNU Lesser General Public License as published
//	by the Free Software Foundation, either version 3 of the License,
//	or (at your option) any later version.
//
//	OpenZWave is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU Lesser General Public License for more details.
//
//	You should have received a copy of the GNU Lesser General Public License
//	along with OpenZWave.  If not, see <http://www.gnu.org/licenses/>.
//
//-----------------------------------------------------------------------------
// Other modifications completed by conradvassallo.com, then thomasloughlin.com
// This version is by willemburgers.nl

#include "Main.h"

//C/C++ system includes:
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <cstring>
#include <string>
#include <sstream>
#include <iterator>
#include <stdio.h>
#include <iostream>
#include <vector>
#include <stdlib.h>
#include <stdexcept>
#include <signal.h>
#include <limits>

//External classes and libs
#include <libwebsockets.h>
#include <json/json.h>
#include <libsocket/unixclientstream.hpp>
#include <libsocket/inetclientstream.hpp>
#include <libsocket/inetserverstream.hpp>

//Open-Zwave includes:
#include "Options.h"
#include "Manager.h"
#include "Driver.h"
#include "Node.h"
#include "Scene.h"
#include "Group.h"
#include "Notification.h"

//Project includes
#include "Sunrise.h"
#include "Configuration.h"
#include "ProtocolException.h"

using namespace OpenZWave;

//-----------------------------------------------------------------------------
// OpenZwave NodeInfo struct
//-----------------------------------------------------------------------------
typedef struct {
	uint32			m_homeId;
	uint8			m_nodeId;
	uint8			m_basicmapping;
	bool			m_needsSync;
	time_t			m_LastSeen;
	bool			m_polled;
	list<ValueID>	m_values;
} NodeInfo;

//-----------------------------------------------------------------------------
// Alarms in this Open-Zwave server have a time and a description
//-----------------------------------------------------------------------------
struct Alarm {
	time_t			alarmtime;
	std::string		description;
	bool operator<(Alarm const &other) { return alarmtime < other.alarmtime; }
	bool operator==(Alarm const &other) { return (strcmp(description.c_str(), other.description.c_str()) == 0); }
};

//-----------------------------------------------------------------------------
// Rooms in this Open-Zwave server have a name and a thermostat setpoint
//-----------------------------------------------------------------------------
struct Room {
	std::string		name;
	float 			setpoint;
	float			currentTemp;
	bool			changed;
	bool operator<(Room const &other) { return strcmp(name.c_str(), other.name.c_str()) < 0; }
	bool operator==(Room const &other) { return (strcmp(name.c_str(), other.name.c_str()) == 0); }
};

//-----------------------------------------------------------------------------
// Scenes in this Open-Zwave server have a name and only one is running
//-----------------------------------------------------------------------------
struct SceneListItem {
	std::string		name;
	bool			active;
};

//-----------------------------------------------------------------------------
// Cached values of Wake-up Intervals
//-----------------------------------------------------------------------------
struct WakeupIntervalCacheItem {
	uint8			nodeId;
	int				interval;
	int				defaultInterval;
	int				minInterval;
	int				maxInterval;
};

//-----------------------------------------------------------------------------
// LibWebSockets messages definitions
//-----------------------------------------------------------------------------
#define MAX_MESSAGE_QUEUE 32

struct LWSMessage {
	std::string			message;
	bool				broadcast;
	struct lws			*wsi;
};

static std::vector<LWSMessage> ringbuffer (MAX_MESSAGE_QUEUE);
static int ringbuffer_head = 0;

struct lws_context *context;

//-----------------------------------------------------------------------------
// definitions
//-----------------------------------------------------------------------------
#define SOCKET_COLLECTION_TIMEOUT 10
#define CACHE_INIT_TIMEOUT 5

static bool stopping = false;
static OZWSS::Configuration* conf;

static uint32 g_homeId = 0;
static bool g_initFailed = false;
static bool atHome = false;
static bool alarmset = false;
static list<Alarm> alarmList;
static list<Room> roomList;
static list<SceneListItem> sceneList;
static std::map<std::string, WakeupIntervalCacheItem> WakeupIntervalCache;
static list<NodeInfo*> g_nodes;
static pthread_mutex_t g_criticalSection;
static pthread_cond_t initCond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t initMutex = PTHREAD_MUTEX_INITIALIZER;

// Value-Defintions of the different String values
enum Commands {Undefined_command = 0, Auth, AList, SetNode, RoomListC, RoomC, Plus, Minus, SceneListC, SceneC, Create, Add, Remove, Activate, ControllerC, Cancel, Reset, Cron, Switch, AtHome, PollInterval, AlarmList, Test, Exit};
enum Triggers {Undefined_trigger = 0, Sunrise, Sunset, Thermostat, Update, Cache_init};
enum DeviceOptions {Undefined_Option = 0, Name, Location, SwitchC, Level, Thermostat_Setpoint, Polling, Wake_up_Interval, Battery_report};
static std::map<std::string, Commands> s_mapStringCommands;
static std::map<std::string, Triggers> s_mapStringTriggers;
static std::map<std::string, DeviceOptions> s_mapStringOptions;
static std::map<std::string, int> MapCommandClassBasic;

void create_string_maps() {
	s_mapStringCommands["AUTH"] = Auth;
	s_mapStringCommands["ALIST"] = AList;
	s_mapStringCommands["SETNODE"] = SetNode;
	s_mapStringCommands["ROOMLIST"] = RoomListC;
	s_mapStringCommands["ROOM"] = RoomC;
	s_mapStringCommands["PLUS"] = Plus;
	s_mapStringCommands["MINUS"] = Minus;
	s_mapStringCommands["SCENELIST"] = SceneListC;
	s_mapStringCommands["SCENE"] = SceneC;
	s_mapStringCommands["CREATE"] = Create;
	s_mapStringCommands["ADD"] = Add;
	s_mapStringCommands["REMOVE"] = Remove;
	s_mapStringCommands["ACTIVATE"] = Activate;
	s_mapStringCommands["CONTROLLER"] = ControllerC;
	s_mapStringCommands["CANCEL"] = Cancel;
	s_mapStringCommands["RESET"] = Reset;
	s_mapStringCommands["CRON"] = Cron;
	s_mapStringCommands["SWITCH"] = Switch;
	s_mapStringCommands["ATHOME"] = AtHome;
	s_mapStringCommands["POLLINTERVAL"] = PollInterval;
	s_mapStringCommands["ALARMLIST"] = AlarmList;
	s_mapStringCommands["TEST"] = Test;
	s_mapStringCommands["EXIT"] = Exit;

	s_mapStringTriggers["Sunrise"] = Sunrise;
	s_mapStringTriggers["Sunset"] = Sunset;
	s_mapStringTriggers["Thermostat"] = Thermostat;
	s_mapStringTriggers["Update"] = Update;
	s_mapStringTriggers["Cache init"] = Cache_init;

	s_mapStringOptions["Name"] = Name;
	s_mapStringOptions["Location"] = Location;
	s_mapStringOptions["Switch"] = SwitchC;
	s_mapStringOptions["Level"] = Level;
	s_mapStringOptions["Thermostat Setpoint"] = Thermostat_Setpoint;
	s_mapStringOptions["Polling"] = Polling;
	s_mapStringOptions["Wake-up Interval"] = Wake_up_Interval;
	s_mapStringOptions["Battery report"] = Battery_report;

	MapCommandClassBasic["0x03|0x11"] = 0x94;
	MapCommandClassBasic["0x03|0x12"] = 0x30;
	MapCommandClassBasic["0x08|0x02"] = 0x40;
	MapCommandClassBasic["0x08|0x03"] = 0x46;
	MapCommandClassBasic["0x08|0x04"] = 0x43;
	MapCommandClassBasic["0x08|0x05"] = 0x40;
	MapCommandClassBasic["0x08|0x06"] = 0x40;
	MapCommandClassBasic["0x09|0x01"] = 0x50;
	MapCommandClassBasic["0x10"] = 0x25;
	MapCommandClassBasic["0x11"] = 0x26;
	MapCommandClassBasic["0x12|0x01"] = 0x25;
	MapCommandClassBasic["0x12|0x02"] = 0x26;
	MapCommandClassBasic["0x12|0x03"] = 0x28;
	MapCommandClassBasic["0x12|0x04"] = 0x29;
	MapCommandClassBasic["0x13|0x01"] = 0x28;
	MapCommandClassBasic["0x13|0x02"] = 0x29;
	MapCommandClassBasic["0x16|0x01"] = 0x39;
	MapCommandClassBasic["0x20"] = 0x30;
	MapCommandClassBasic["0x21"] = 0x31;
	MapCommandClassBasic["0x30"] = 0x35;
	MapCommandClassBasic["0x31|0x01"] = 0x32;
	MapCommandClassBasic["0x40|0x01"] = 0x62;
	MapCommandClassBasic["0x40|0x02"] = 0x62;
	MapCommandClassBasic["0x40|0x03"] = 0x62;
	MapCommandClassBasic["0xa1"] = 0x71;
}

//functions
void OnControllerUpdate(uint8 cs);
void sigint_handler(int sig);
bool init_Rooms();
bool init_Scenes();
bool init_WakeupIntervalCache();
void *websockets_main(void* arg);
void *run_socket(void* arg);
void process_commands(std::string data, Json::Value& message);
bool parse_option(int32 home, int32 node, std::string name, std::string value, bool& save, std::string& err_message);
bool SetValue(int32 home, int32 node, std::string const value, uint8 cmdclass, std::string label, std::string& err_message);
std::string activateScene(string sclabel);
std::string switchAtHome();
bool try_map_basic(int32 home, int32 node);
void SetAlarm(std::string description, time_t alarmtime, bool offset);
void sigalrm_handler(int sig);

//-----------------------------------------------------------------------------
// Common functions that can be used in every other function
//-----------------------------------------------------------------------------
void split(const std::string& s, const std::string& delimiter, std::vector<std::string>& v) {
	std::string::size_type i = 0;
	std::string::size_type j = s.find(delimiter);
	while (j != std::string::npos) {
		v.push_back(s.substr(i, j - i));
		i = j += delimiter.length();
		j = s.find(delimiter, j);
	}
	v.push_back(s.substr(i, s.length()));
}

std::string trim(std::string s) {
	return s.erase(s.find_last_not_of(" \n\r\t") + 1);
}

template <typename T>
T lexical_cast(const std::string& s) {
	std::stringstream ss(s);

	T result;
	if((ss >> result).fail() || !(ss >> std::ws).eof())
	{
		throw std::runtime_error("Bad cast");
	}

	return result;
}

std::string join( const std::vector<std::string>& elements, const char* const separator)
{
    switch (elements.size())
    {
        case 0:
            return "";
        case 1:
            return elements[0];
        default:
            std::ostringstream os;
            std::copy(elements.begin(), elements.end()-1, std::ostream_iterator<std::string>(os, separator));
            os << *elements.rbegin();
            return os.str();
    }
}

//-----------------------------------------------------------------------------
// <GetNodeInfo>
// retrieve information about a node from the NodeInfo list
//-----------------------------------------------------------------------------
NodeInfo* GetNodeInfo(uint32 const homeId, uint8 const nodeId) {
	for(list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it) {
		NodeInfo* nodeInfo = *it;
		if((nodeInfo->m_homeId == homeId) && (nodeInfo->m_nodeId == nodeId)) {
			return nodeInfo;
		}
	}

	return NULL;
}

NodeInfo* GetNodeInfo(Notification const* notification) {
	uint32 const homeId = notification->GetHomeId();
	uint8 const nodeId = notification->GetNodeId();
	return GetNodeInfo(homeId, nodeId);
}

//-----------------------------------------------------------------------------
// <OnNotification>
// Callback that is triggered when a value, group or node changes
//-----------------------------------------------------------------------------
void OnNotification(Notification const* _notification, void* _context) {
	// Must do this inside a critical section to avoid conflicts with the main thread
	pthread_mutex_lock(&g_criticalSection);

	switch(_notification->GetType()) {
		case Notification::Type_ValueAdded:
		{
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				// Add the new value to our list
				nodeInfo->m_values.push_back( _notification->GetValueID());
			}
			break;
		}

		case Notification::Type_ValueRemoved:
		{
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				// Remove the value from out list
				for(list<ValueID>::iterator it = nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it) {
					if((*it) == _notification->GetValueID()) {
						nodeInfo->m_values.erase(it);
						break;
					}
				}
			}
			break;
		}

		case Notification::Type_ValueChanged:
		{
			// One of the node values has changed
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				nodeInfo->m_LastSeen = time( NULL );
				ValueID vid = _notification->GetValueID();
				for(list<ValueID>::iterator vit = nodeInfo->m_values.begin(); vit != nodeInfo->m_values.end(); ++vit) {
					if((*vit) == vid) {
						nodeInfo->m_values.erase(vit);
						break;
					}
				}
				nodeInfo->m_values.push_back(vid);

				if(strcmp(Manager::Get()->GetNodeType(_notification->GetHomeId(), _notification->GetNodeId()).c_str(), "Setpoint Thermostat") == 0 && strcmp(Manager::Get()->GetValueLabel(vid).c_str(), "Heating 1") == 0) {
					std::string location = Manager::Get()->GetNodeLocation(_notification->GetHomeId(), _notification->GetNodeId());
					float currentSetpoint = 20.00;
					if(Manager::Get()->GetValueAsFloat(vid,&currentSetpoint)) {
						for(list<Room>::iterator rit=roomList.begin(); rit!=roomList.end(); ++rit) {
							if(strcmp(location.c_str(), rit->name.c_str()) != 0) {
								continue;
							}
							if(rit->setpoint != currentSetpoint) {
								rit->setpoint = currentSetpoint;
								std::cout << "Changing setpoint for room " << location << endl;

								for(list<NodeInfo*>::iterator nit = g_nodes.begin(); nit != g_nodes.end(); ++nit) {
									if(_notification->GetHomeId() == (*nit)->m_homeId && _notification->GetNodeId() == (*nit)->m_nodeId) {
										continue;
									}
									if(strcmp(location.c_str(), Manager::Get()->GetNodeLocation(g_homeId, (*nit)->m_nodeId).c_str()) != 0) {
										continue;
									}
									if(strcmp(Manager::Get()->GetNodeType(g_homeId, (*nit)->m_nodeId).c_str(), "Setpoint Thermostat") !=0) {
										continue;
									}
									stringstream ssCurrentSetpoint;
									ssCurrentSetpoint << rit->setpoint;
									string err_message = "";
									if(!SetValue(g_homeId, (*nit)->m_nodeId, ssCurrentSetpoint.str(), COMMAND_CLASS_THERMOSTAT_SETPOINT, "Heating 1", err_message)) {
										std::cout << err_message;
									}
								}
							}
						}
					}
				}

				if(strcmp(Manager::Get()->GetValueLabel(vid).c_str(), "Temperature") == 0) {
					std::string location = Manager::Get()->GetNodeLocation(_notification->GetHomeId(), _notification->GetNodeId());
					float currentTemp = 20.00;
					if(Manager::Get()->GetValueAsFloat(vid,&currentTemp)) {
						for(list<Room>::iterator rit=roomList.begin(); rit!=roomList.end(); ++rit) {
							if(strcmp(location.c_str(), rit->name.c_str()) != 0) {
								continue;
							}
							if(rit->currentTemp != currentTemp) {
								rit->currentTemp = currentTemp;
								std::cout << "Changing current temp for room " << location << endl;
							}
						}
					}
				}

				if(strcmp(Manager::Get()->GetValueLabel(vid).c_str(), "Wake-up Interval") == 0) {
					stringstream key;
					key << (int) _notification->GetHomeId() << (int) _notification->GetNodeId();
					if(WakeupIntervalCache.count(key.str()) == 0)
					{
						SetAlarm("Cache init", CACHE_INIT_TIMEOUT, true);
					}
					else
					{
						WakeupIntervalCacheItem cacheItem = WakeupIntervalCache[key.str()];
						int interval;
						if(Manager::Get()->GetValueAsInt(vid,&interval) && cacheItem.interval != interval) {
							stringstream ssInterval;
							ssInterval << cacheItem.interval;
							string err_message = "";
							if(!SetValue(g_homeId, nodeInfo->m_nodeId, ssInterval.str(),COMMAND_CLASS_WAKE_UP, "Wake-up Interval", err_message)) {
								std::cout << err_message;
							}
						}
					}
				}

				SetAlarm("Update", SOCKET_COLLECTION_TIMEOUT, true);
			}
			break;
		}

		case Notification::Type_Group:
		{
			// One of the node's association groups has changed
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				nodeInfo = nodeInfo;			// placeholder for real action
			}
			break;
		}

		case Notification::Type_NodeAdded:
		{
			// Add the new node to our list
			NodeInfo* nodeInfo = new NodeInfo();
			nodeInfo->m_homeId = _notification->GetHomeId();
			nodeInfo->m_nodeId = _notification->GetNodeId();
			nodeInfo->m_polled = false;
			nodeInfo->m_needsSync = false;
			g_nodes.push_back(nodeInfo);
			break;
		}

		case Notification::Type_NodeRemoved:
		{
			// Remove the node from our list
			uint32 const homeId = _notification->GetHomeId();
			uint8 const nodeId = _notification->GetNodeId();
			for(list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it) {
				NodeInfo* nodeInfo = *it;
				if((nodeInfo->m_homeId == homeId) && (nodeInfo->m_nodeId == nodeId)) {
					g_nodes.erase(it);
					delete nodeInfo;
					break;
				}
			}

			WakeupIntervalCache.clear();
			init_WakeupIntervalCache();

			break;
		}

		case Notification::Type_NodeEvent:
		{
			// We have received an event from the node, caused by a
			// basic_set or hail message.
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				nodeInfo->m_LastSeen = time( NULL );
			}
			break;
		}

		case Notification::Type_PollingDisabled:
		{
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				nodeInfo->m_polled = false;
			}
			break;
		}

		case Notification::Type_PollingEnabled:
		{
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				nodeInfo->m_polled = true;
			}
			break;
		}

		case Notification::Type_DriverReady:
		{
			g_homeId = _notification->GetHomeId();
			break;
		}

		case Notification::Type_DriverFailed:
		{
			g_initFailed = true;
			pthread_cond_broadcast(&initCond);
			break;
		}

		case Notification::Type_AwakeNodesQueried:
		case Notification::Type_AllNodesQueried:
		case Notification::Type_AllNodesQueriedSomeDead:
		{
			pthread_cond_broadcast(&initCond);
			break;
		}

		case Notification::Type_NodeProtocolInfo:
		{
			char buffer[10];
			uint32 const homeId = _notification->GetHomeId();
			uint8 const nodeId = _notification->GetNodeId();
			if(NodeInfo* nodeInfo = GetNodeInfo(homeId, nodeId)) {

				uint8 generic = Manager::Get()->GetNodeGeneric(homeId , nodeId);
				uint8 specific = Manager::Get()->GetNodeSpecific(homeId, nodeId);

				snprintf(buffer, 10, "0x%02X|0x%02X", generic, specific);
				if(MapCommandClassBasic.find(buffer) != MapCommandClassBasic.end()) {
					nodeInfo->m_basicmapping = MapCommandClassBasic[buffer];
				}
				else {
					// We didn't find a Generic+Specifc in the table, now we check
					// for Generic only
					snprintf(buffer, 10, "0x%02X", generic);

					// Check if we have a mapping in our map table
					if(MapCommandClassBasic.find(buffer) != MapCommandClassBasic.end()) {
						nodeInfo->m_basicmapping = MapCommandClassBasic[buffer];
					}
				}

				nodeInfo->m_LastSeen = time(NULL);
			}
			break;
		}

		case Notification::Type_ControllerCommand:
		{
			OnControllerUpdate(_notification->GetEvent());
			break;
		}

		case Notification::Type_Notification:
			switch(_notification->GetNotification()) {
				case Notification::Code_Awake: {
				}
				default: {
				}
			}
		case Notification::Type_DriverReset:
		case Notification::Type_NodeNaming:
		case Notification::Type_NodeQueriesComplete: {
			if(NodeInfo* nodeInfo = GetNodeInfo(_notification)) {
				nodeInfo->m_LastSeen = time(NULL);
			}
			break;
		}
		default: {
		}
	}

	pthread_mutex_unlock(&g_criticalSection);
}

void OnControllerUpdate(uint8 cs) {
	switch (cs) {
		case Driver::ControllerState_Normal:
		{
			std::cout << "ControllerState: Normal - No command in progress." << endl;
			break;
		}
		case Driver::ControllerState_Starting:
		{
			std::cout << "ControllerState: Starting - The command is starting." << endl;
			break;
		}
		case Driver::ControllerState_Cancel:
		{
			std::cout << "ControllerState: Cancel - The command was cancelled." << endl;
			break;
		}
		case Driver::ControllerState_Error:
		{
			std::cout << "ControllerState: Error - Command invocation had error(s) and was aborted." << endl;
			break;
		}
		case Driver::ControllerState_Sleeping:
		{
			std::cout << "ControllerState: Sleeping - Controller command is on a sleep queue wait for device." << endl;
			break;
		}
		case Driver::ControllerState_Waiting:
		{
			std::cout << "ControllerState: Waiting - Controller is waiting for a user action." << endl;
			break;
		}
		case Driver::ControllerState_InProgress:
		{
			std::cout << "ControllerState: InProgress - The controller is communicating with the other device to carry out the command." << endl;
			break;
		}
		case Driver::ControllerState_Completed:
		{
			std::cout << "ControllerState: Completed - The command has completed successfully." << endl;
			break;
		}
		case Driver::ControllerState_Failed:
		{
			std::cout << "ControllerState: Failed - The command has failed." << endl;
			break;
		}
		default:
		{
			std::cout << "ControllerState: Unknown" << endl;
			break;
		}
	}
}

//-----------------------------------------------------------------------------
// Libwebsockets definitions
//-----------------------------------------------------------------------------

static int httpCallback(	struct lws *wsi,
							enum lws_callback_reasons reason,
							void *user, void *in, size_t len) {
	return 0;
}

struct per_session_data__open_zwave {
	int ringbuffer_tail;
	bool authenticated;
};

static int open_zwaveCallback(	struct lws *wsi,
								enum lws_callback_reasons reason,
								void *user, void *in, size_t len) {
	uint n;
	struct per_session_data__open_zwave *pss = (struct per_session_data__open_zwave *)user;

	// reason for callback
	switch(reason) {
		case LWS_CALLBACK_ESTABLISHED: {
			pss->ringbuffer_tail = ringbuffer_head;
			pss->authenticated = false;
			std::cout << "WebSocket connection established" << endl;
			break;
		}
		case LWS_CALLBACK_RECEIVE: {
			// log what we recieved.
			printf("Received websocket data: %s\n", (char*) in);

			std::string data = (char*) in;
			Json::Value message;

			try {
				if(pss->authenticated || data.compare(0,4,"AUTH") == 0)
					process_commands(data, message);
			}
			catch (OZWSS::ProtocolException& e) {
				message["error"]["err_main"] = "ProtocolException";
				message["error"]["err_message"] = e.what();
			}
			catch (std::exception const& e) {
				std::cout << "Exception: " << e.what() << endl;
			}

			if(data.compare(0,4,"AUTH") == 0 && message["auth"] == true) {
				pss->authenticated = true;
			}

			std::string response;
			Json::FastWriter fastWriter;
			response = fastWriter.write(message);

			//put the response in the writebuffer
			LWSMessage lwsresponse;
			lwsresponse.message = response;
			lwsresponse.broadcast = false;
			lwsresponse.wsi = wsi;

			ringbuffer[ringbuffer_head] = lwsresponse;
			if (ringbuffer_head == (MAX_MESSAGE_QUEUE - 1)) {
				ringbuffer_head = 0;
			}
			else {
				ringbuffer_head++;
			}

			lws_callback_on_writable_all_protocol(lws_get_context(wsi), lws_get_protocol(wsi));
			break;
		}
		case LWS_CALLBACK_SERVER_WRITEABLE: {
			while (pss->ringbuffer_tail != ringbuffer_head) {
				LWSMessage lwsmessage = ringbuffer[pss->ringbuffer_tail];
				if(pss->authenticated && (lwsmessage.broadcast || (lwsmessage.wsi == wsi))) {
					char buf[LWS_SEND_BUFFER_PRE_PADDING + lwsmessage.message.length() + LWS_SEND_BUFFER_POST_PADDING];

					memcpy(&buf[LWS_SEND_BUFFER_PRE_PADDING], lwsmessage.message.c_str(),
						   lwsmessage.message.length());

					n = lws_write(wsi,
						(unsigned char *) &buf[LWS_SEND_BUFFER_PRE_PADDING],
						lwsmessage.message.length(),
						LWS_WRITE_TEXT);
					if (n < 0) {
						lwsl_err("ERROR %d writing to mirror socket\n", n);
						return -1;
					}
					if (n < lwsmessage.message.length()) {
						lwsl_err("mirror partial write %d vs %d\n",
							n, lwsmessage.message.length());
					}
				}

				if (pss->ringbuffer_tail == (MAX_MESSAGE_QUEUE - 1))
					pss->ringbuffer_tail = 0;
				else
					pss->ringbuffer_tail++;

				if (((ringbuffer_head - pss->ringbuffer_tail) &
					  (MAX_MESSAGE_QUEUE - 1)) == (MAX_MESSAGE_QUEUE - 15))
					lws_rx_flow_allow_all_protocol(lws_get_context(wsi), lws_get_protocol(wsi));

				if (lws_send_pipe_choked(wsi)) {
					lws_callback_on_writable(wsi);
					break;
				}
				/*
				 * for tests with chrome on same machine as client and
				 * server, this is needed to stop chrome choking
				 */
				usleep(1);
			}
			break;
		}
		case LWS_CALLBACK_CLOSED: {
			std::cout << "Websocket client closed the connection" << endl;
			break;
		}
		case LWS_CALLBACK_PROTOCOL_DESTROY: {
			std::cout << "open-zwave protocol not used anymore" << endl;
		}
		default:
			break;
	}
	return 0;
}

// protocol types for websockets
static struct lws_protocols protocols[] = {
	{
		"http-only",
		httpCallback,
		0
	},
	{
		"open-zwave",
		open_zwaveCallback,
		sizeof(struct per_session_data__open_zwave),
		0
	},
	{
		NULL, NULL, 0
	}
};

//-----------------------------------------------------------------------------
// <main>
// Create the driver, wait for the library to complete the initialization
// Then create the socket server and the websocket server separately
//-----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
	string confPath = "./Config.ini";
	string port = "/dev/ttyUSB0";
	if(argc > 1) {
		for(int argIndex = 1; argIndex < argc; ++argIndex) {
			if (argIndex + 1 != argc) {
				if(string(argv[argIndex]) == "-confPath") {
					confPath = string(argv[++argIndex]);
				}
				else if(string(argv[argIndex]) == "-serialPort") {
					port = string(argv[++argIndex]);
				}
				else {
					std::cout << "Not enough or invalid arguments, please try again.\n";
					exit(0);
				}
			}
			else {
				std::cout << "Not enough or invalid arguments, please try again.\n";
				exit(0);
			}
		}
	}

	struct sigaction sigIntHandler;
	sigIntHandler.sa_handler = sigint_handler;
	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_flags = 0;
	sigaction(SIGINT, &sigIntHandler, NULL);

	conf = new OZWSS::Configuration(confPath);
	pthread_mutexattr_t mutexattr;

	pthread_mutexattr_init(&mutexattr);
	pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&g_criticalSection, &mutexattr);
	pthread_mutexattr_destroy(&mutexattr);

	pthread_mutex_lock(&initMutex);

	// Create the OpenZWave Manager.
	// The first argument is the path to the config files (where the manufacturer_specific.xml file is located
	// The second argument is the path for saved Z-Wave network state and the log file. If you leave it NULL
	// the log file will appear in the program's working directory.
	Options::Create("/usr/local/etc/openzwave/", "", "");
	Options::Get()->AddOptionInt("RetryTimeout", 5000);
	Options::Get()->Lock();

	Manager::Create();

	// Add a callback handler to the manager.  The second argument is a context that
	// is passed to the OnNotification method.  If the OnNotification is a method of
	// a class, the context would usually be a pointer to that class object, to
	// avoid the need for the notification handler to be a static.
	Manager::Get()->AddWatcher(OnNotification, NULL);

	// Add a Z-Wave Driver
	Manager::Get()->AddDriver(port);
	//Manager::Get()->AddDriver( "HID Controller", Driver::ControllerInterface_Hid );

	// Set the default poll interval to 30 minutes
	Manager::Get()->SetPollInterval(1000*60*30, false); //default to 30 minutes

	// Now we just wait for the driver to become ready, and then write out the loaded config.
	// In a normal app, we would be handling notifications and building a UI for the user.
	pthread_cond_wait(&initCond, &initMutex);

	if(!g_initFailed) {
		create_string_maps();
		if(!init_Rooms()) {
			std::cerr << "Something went wrong configuring the Rooms";
			return 0;
		}
		if(!init_Scenes()) {
			std::cerr << "Something went wrong configuring the Scenes";
			return 0;
		}
		if(!init_WakeupIntervalCache()) {
			std::cerr << "Something went wrong configuring the Wake-up Interval Cache";
			return 0;
		}
		Manager::Get()->WriteConfig(g_homeId);

		Driver::DriverData data;
		Manager::Get()->GetDriverStatistics(g_homeId, &data);

		printf("SOF: %d ACK Waiting: %d Read Aborts: %d Bad Checksums: %d\n", data.m_SOFCnt, data.m_ACKWaiting, data.m_readAborts, data.m_badChecksum);
		printf("Reads: %d Writes: %d CAN: %d NAK: %d ACK: %d Out of Frame: %d\n", data.m_readCnt, data.m_writeCnt, data.m_CANCnt, data.m_NAKCnt, data.m_ACKCnt, data.m_OOFCnt);
		printf("Dropped: %d Retries: %d\n", data.m_dropped, data.m_retries);
		printf("***************************************************** \n");

		//start the websocket in a new thread
		pthread_t websocket_thread;
		if(pthread_create(&websocket_thread , NULL ,  websockets_main ,NULL) < 0) {
			throw std::runtime_error("Unable to create thread");
		}
		else {
			std::cout << "Websocket starting" << endl;
		}

		string tcpport;
		if(!conf->GetTCPPort(tcpport)) {
			std::cerr << "There is no TCP port set in Config.ini, please specify one and try again.\n";
			return 0;
		}
		std::cout << "Starting TCP server on port: " << tcpport << endl;
		string host = "0.0.0.0";
		using libsocket::inet_stream_server;
		using libsocket::inet_stream;

		inet_stream_server srv(host,tcpport,LIBSOCKET_IPv4);

		while(!stopping) {

			inet_stream* client = srv.accept();

			pthread_t thread;
			try {
				if(pthread_create(&thread, NULL, run_socket, (void*) client) < 0) {
					throw std::runtime_error("Unable to create thread");
				}
				else {
					std::cout << "Socket connection established" << endl;
				}
			}
			catch(...) {
				std::cout << "Other exception" << endl;
			}
		}
		srv.destroy();
	}

	// program exit (clean up)
	delete conf;
	Manager::Get()->WriteConfig(g_homeId);
	std::cout << "Closing connection to Zwave Controller" << endl;

	if(strcasecmp(port.c_str(), "usb") == 0) {
		Manager::Get()->RemoveDriver("HID Controller");
	}
	else {
		Manager::Get()->RemoveDriver(port);
	}
	Manager::Get()->RemoveWatcher(OnNotification, NULL);
	Manager::Destroy();
	Options::Destroy();
	pthread_mutex_destroy(&g_criticalSection);
	return 0;
}

//-----------------------------------------------------------------------------
// sigint_handler
// Handles SIGINT program termination (CTRL+C)
//-----------------------------------------------------------------------------
void sigint_handler(int sig) {
	stopping = true;
}

//-----------------------------------------------------------------------------
// init_Rooms
// Create a list of rooms for the program to maintain
// The roomlist is built from the devicelist information
//-----------------------------------------------------------------------------
bool init_Rooms() {
	for(list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it) {
		std::string location = Manager::Get()->GetNodeLocation(g_homeId, (*it)->m_nodeId);
		if(location.empty()) {
			continue;
		}

		float currentSetpoint=0.0;
		float currentTemp=0.0;
		for(list<ValueID>::iterator vit = (*it)->m_values.begin(); vit != (*it)->m_values.end(); ++vit) {
			if(strcmp(Manager::Get()->GetNodeType(g_homeId, (*it)->m_nodeId).c_str(), "Setpoint Thermostat") ==0 && strcmp(Manager::Get()->GetValueLabel(*vit).c_str(), "Heating 1") == 0) {
				if(!Manager::Get()->GetValueAsFloat(*vit, &currentSetpoint)) {
					return false;
				}
			}
			else if(strcmp(Manager::Get()->GetValueLabel(*vit).c_str(), "Temperature") == 0) {
				if(!Manager::Get()->GetValueAsFloat(*vit, &currentTemp)) {
					return false;
				}
			}
		}

		Room newroom;
		newroom.name = location;
		newroom.setpoint = currentSetpoint;
		newroom.currentTemp = currentTemp;
		newroom.changed = false;

		list<Room>::iterator rit;
		for(rit = roomList.begin(); rit != roomList.end(); ++rit)
		{
			if((*rit)==newroom)
				break;
		}
		if ( rit != roomList.end() )
		{
			if(currentSetpoint!=0.0) {
				rit->setpoint = currentSetpoint;
			}
			if(currentTemp!=0.0) {
				rit->currentTemp = currentTemp;
			}
		}
		else {
			roomList.push_back(newroom);
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
// init_Scenes
// Create a list of scenes for the program to maintain
//-----------------------------------------------------------------------------
bool init_Scenes() {
	uint8 numscenes = 0;
	uint8 *sceneIds = new uint8[numscenes];

	if((numscenes = Manager::Get()->GetAllScenes(&sceneIds))==0) {
		std::cout << "No Scenes found" << endl;
	}

	int scid=0;

	for(int i=0; i<numscenes; ++i) {
		scid = sceneIds[i];
		SceneListItem newScene;
		newScene.name = Manager::Get()->GetSceneLabel(scid);
		newScene.active = false;
		sceneList.push_back(newScene);
	}
	delete sceneIds;
	return true;
}

//-----------------------------------------------------------------------------
// init_WakeupIntervalCache
// Create a list of Wake-up Intervals
// When bateries are changed, Wake-up Intervals reset to default
// This cache makes sure that the device is set to the value you chose instead of the default value
//-----------------------------------------------------------------------------
bool init_WakeupIntervalCache() {
	for(list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it) {
		int defaultInterval = 0, minInterval = 0, maxInterval = std::numeric_limits<int>::max(), interval = 0;
		bool wake_cc_node = false;
		for(list<ValueID>::iterator vit = (*it)->m_values.begin(); vit != (*it)->m_values.end(); ++vit) {
			if((*vit).GetCommandClassId() != COMMAND_CLASS_WAKE_UP) {
				continue;
			}
			wake_cc_node = true;
			if(strcmp(Manager::Get()->GetValueLabel(*vit).c_str(), "Wake-up Interval") == 0) {
				if(!Manager::Get()->GetValueAsInt((*vit), &interval)) {
					return false;
				}
				std::cout << "Interval: " << interval << endl;
			}
			if(strcmp(Manager::Get()->GetValueLabel(*vit).c_str(), "Default Wake-up Interval") == 0) {
				if(!Manager::Get()->GetValueAsInt((*vit), &defaultInterval)) {
					return false;
				}
			}
			if(strcmp(Manager::Get()->GetValueLabel(*vit).c_str(), "Minimum Wake-up Interval") == 0) {
				if(!Manager::Get()->GetValueAsInt((*vit), &minInterval)) {
					return false;
				}
			}
			if(strcmp(Manager::Get()->GetValueLabel(*vit).c_str(), "Maximum Wake-up Interval") == 0) {
				if(!Manager::Get()->GetValueAsInt((*vit), &maxInterval)) {
					return false;
				}
			}
		}
		if(wake_cc_node)
		{
			stringstream key;
			key << (int) g_homeId << (int) (*it)->m_nodeId;
			WakeupIntervalCacheItem newCacheItem;
			newCacheItem.nodeId = (*it)->m_nodeId;
			newCacheItem.interval = interval;
			newCacheItem.defaultInterval = defaultInterval;
			newCacheItem.minInterval = minInterval;
			newCacheItem.maxInterval = maxInterval;
			WakeupIntervalCache.insert(std::pair<std::string, WakeupIntervalCacheItem>(key.str(),newCacheItem));
		}
	}
	return true;
}

//-----------------------------------------------------------------------------
// websockets_main
// Start the websocket server and keep the service thread running
//-----------------------------------------------------------------------------
void *websockets_main(void* arg) {
	int port;
	if(!conf->GetWSPort(port)) {
		std::cerr << "There is no WS port set in Config.ini, please specify one and try again.\n";
		return 0;
	}
	const char *interface = NULL;

	const char *cert_path = NULL;
	const char *key_path = NULL;

	std::string certificate, certificate_key;
	conf->GetCertificateInfo(certificate, certificate_key);

	if(!certificate.empty() && !certificate_key.empty()) {
		cert_path = certificate.c_str();
		key_path = certificate_key.c_str();
	}

	// no special options
	int opts = 0;

	// create connection struct
	struct lws_context_creation_info info;
	memset(&info, 0, sizeof info);
	info.port = port;
	info.iface = interface;
	info.protocols = protocols;
	info.extensions = NULL;
	info.ssl_cert_filepath = cert_path;
	info.ssl_private_key_filepath = key_path;
	info.options = opts;

	// create libwebsocket context representing this server
	context = lws_create_context(&info);

	// make sure it starts
	if(context == NULL) {
		std::cerr << "libwebsocket init failed\n";
		stopping = true; //notify the other threads
		return 0;
	}
	std::cout << "starting websocket server...\n";

	// infinite loop, to end this server send SIGTERM. (CTRL+C)
	while (!stopping) {
		lws_service(context, 10);
	}

	lws_context_destroy(context);

	return 0;
}

//-----------------------------------------------------------------------------
// run_socket
// Every new socket connection gets its own thread
// This thread listens for commands on the socket connection
//-----------------------------------------------------------------------------
void *run_socket(void* arg) {
	using libsocket::inet_stream;
	inet_stream* client;
	client = (inet_stream*) arg;

	while(!stopping) {
		try { // command parsing errors
			//get commands from the socket
			std::string data;
			data.resize(1024);
			*client >> data;

			if(data.empty()) {
				std::cout << "Socket client closed the connection" << endl;
				client->destroy();
				return 0;
			}
			std::cout << "Received socket data: " << data;
			Json::Value message;
			process_commands(data, message);
			Json::FastWriter fastWriter;
			string response = fastWriter.write(message) + "\n";
			*client << response;
		}
		catch (OZWSS::ProtocolException& e) {
			string what = "ProtocolException: ";
			what += e.what();
			what += "\n";
			*client << what;
		}
		catch (libsocket::socket_exception exc) {
			std::cerr << exc.mesg << " errno code: " << exc.err;
		}
		catch (std::exception const& e) {
			std::cout << "Exception: " << e.what() << endl;
		}
	}
	std::cout << "Server is stopping, closing socket connection" << endl;
	*client << "Server is stopping, closing socket connection";
	client->destroy();
	return 0;
}

//-----------------------------------------------------------------------------
// process_commands
// when a command comes in, parse it, execute it and send the response back
//-----------------------------------------------------------------------------
void process_commands(std::string data, Json::Value& message) {
	vector<string> v;
	split(data, "~", v);
	message["command"] = trim(v[0]);
	switch (s_mapStringCommands[trim(v[0])])
	{
		case Auth:
		{
			std::string client_id, client_secret;
			if(conf->GetGoogleClientIdAndSecret(client_id, client_secret)) {
				using libsocket::unix_stream_client;
				message["auth"] = false;
				Json::Value gapi_message;
				gapi_message["access_token"] = v[1];
				gapi_message["client_id"] = client_id;
				gapi_message["client_secret"] = client_secret;
				gapi_message["redirect_url"] = "";

				std::string tokeninfo_response(1024,NULL), profile(2048,NULL), closing_data(1024,NULL);

				try {
					unix_stream_client sock("/tmp/gapi.sock");

					Json::FastWriter fastWriter;
					sock << fastWriter.write(gapi_message).c_str();
					sock >> tokeninfo_response;
					sock >> profile;
					sock.shutdown(LIBSOCKET_WRITE);
					sock >> closing_data;

					if(!tokeninfo_response.empty() && !profile.empty())
					{
						message["profile"] = profile;
						message["auth"] = true;
					}
				}
				catch (const libsocket::socket_exception& exc) {
					message["error"]["err_main"] = "Could not authenticate";
					message["error"]["err_message"] = "Failed communicate with the Google api wrapper";
					break;
				}
			}
			break;
		}
		case AList:
		{
			int nodepos = 0;
			for(list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it) {
				NodeInfo* nodeInfo = *it;

				Json::Value node;
				std::string nodeName = Manager::Get()->GetNodeName(g_homeId, nodeInfo->m_nodeId);
				if(nodeName.size() == 0) {
					nodeName = "Undefined";
				}
				std::string nodeType = Manager::Get()->GetNodeType(g_homeId, nodeInfo->m_nodeId);
				std::string manufacturerName = Manager::Get()->GetNodeManufacturerName(g_homeId, nodeInfo->m_nodeId);
				std::string productName = Manager::Get()->GetNodeProductName(g_homeId, nodeInfo->m_nodeId);
				std::string productId = Manager::Get()->GetNodeProductId(g_homeId, nodeInfo->m_nodeId);

				node["Name"] = nodeName;
				node["ID"] = nodeInfo->m_nodeId;
				node["Location"] = Manager::Get()->GetNodeLocation(g_homeId, nodeInfo->m_nodeId);
				node["Type"] = nodeType;
				node["Manufacturer"] = manufacturerName;
				node["ProductName"] = productName;
				node["ProductId"] = productId;

				for(list<ValueID>::iterator vit = nodeInfo->m_values.begin(); vit != nodeInfo->m_values.end(); ++vit) {
					std::string tempstr="";
					Manager::Get()->GetValueAsString(*vit,&tempstr);
					node["Values"][Manager::Get()->GetValueLabel(*vit)] = tempstr;
				}
				stringstream ssNodeLastSeen;
				char buffer[256];
				struct tm * timeinfo;
				timeinfo = localtime(&(nodeInfo->m_LastSeen));
				if(strftime(buffer, 256, "%a %d %b %R", timeinfo) != 0)
				{
					ssNodeLastSeen << buffer;
				}
				else {
					ssNodeLastSeen << trim(ctime(&(nodeInfo->m_LastSeen)));
				}
				node["LastSeen"] = ssNodeLastSeen.str();
				message["nodes"][nodepos] = node;
				++nodepos;
			}
			break;
		}
		case SetNode:
		{
			if(v.size() != 3) {
				throw OZWSS::ProtocolException("Wrong number of arguments", 2);
			}
			int Node = 0;
			string Options = "";

			Node = lexical_cast<int>(v[1]);
			Options=trim(v[2]);
			std::vector<std::string> executedOptions;

			if(!Options.empty()) {
				vector<string> OptionList;
				split(Options, "<>", OptionList);
				bool save = false;

				for(std::vector<string>::iterator it = OptionList.begin(); it != OptionList.end(); ++it) {
					std::size_t found = (*it).find('=');
					if(found!=std::string::npos) {
						std::string name = (*it).substr(0,found);
						std::string value = (*it).substr(found+1);
						std::string err_message = "";
						if(!parse_option(g_homeId, Node, name, value, save, err_message)) {
							message["error"]["err_main"] = "Error while parsing option " + name;
							message["error"]["err_message"] = err_message;
							break;
						}
						else {
							executedOptions.push_back(name);
						}
					}
				}
				if(save) {
					Manager::Get()->WriteConfig(g_homeId);
				}
			}

			stringstream ssNode;
			ssNode << Node;

			message["text"] = "The following options have been set for Node " + ssNode.str() + ": " + join(executedOptions, ", ");
			break;
		}
		case RoomListC:
		{
			Json::Value rooms(Json::arrayValue);
			for(list<Room>::iterator rit=roomList.begin(); rit!=roomList.end(); ++rit) {
				Json::Value room;
				room["Name"] = rit->name;
				room["currentSetpoint"] = rit->setpoint;
				stringstream ssCurrentTemp;
				ssCurrentTemp << rit->currentTemp;
				room["currentTemp"] = ssCurrentTemp.str();
				rooms.append(room);
			}
			message["rooms"] = rooms;
			break;
		}
		case RoomC:
		{
			if(v.size() != 3) {
				throw OZWSS::ProtocolException("Wrong number of arguments", 2);
			}
			std::string location = trim(v[2]);
			for(list<Room>::iterator rit=roomList.begin(); rit!=roomList.end(); ++rit) {
				if(strcmp(location.c_str(), rit->name.c_str()) !=0) {
					continue;
				}

				switch(s_mapStringCommands[trim(v[1])])
				{
					case Plus:
						rit->setpoint += 0.5;
						rit->changed = true;
						break;
					case Minus:
						rit->setpoint -= 0.5;
						rit->changed = true;
						break;
					default:
						throw OZWSS::ProtocolException("Unknown Room command", 1);
						break;
				}
				stringstream setpoint, currentTemp;
				setpoint << rit->setpoint;
				currentTemp << rit->currentTemp;
				message["room"]["Name"] = location;
				message["room"]["currentSetpoint"] = rit->setpoint;
				stringstream ssCurrentTemp;
				ssCurrentTemp << rit->currentTemp;
				message["room"]["currentTemp"] = ssCurrentTemp.str();
				std::cout << "Room " << location << " termperature setpoint set to " << rit->setpoint << endl;
			}

			SetAlarm("Thermostat", SOCKET_COLLECTION_TIMEOUT, true);
			SetAlarm("Update", SOCKET_COLLECTION_TIMEOUT+1, true);
			break;
		}
		case SceneListC:
		{
			int scenepos = 0;
			for(list<SceneListItem>::iterator sliit=sceneList.begin(); sliit!=sceneList.end(); ++sliit) {
				Json::Value scene;
				scene["Name"] = sliit->name;
				scene["Active"] = sliit->active;
				message["scenes"][scenepos] = scene;
				++scenepos;
			}
			break;
		}
		case SceneC:
		{
			if(v.size() < 3) {
				throw OZWSS::ProtocolException("Wrong number of arguments", 2);
			}
			switch(s_mapStringCommands[trim(v[1])])
			{
				case Create:
				{
					string sclabel = trim(v[2]);
					if(int scid = Manager::Get()->CreateScene()) {
						stringstream ssID;
						ssID << scid;
						Manager::Get()->SetSceneLabel(scid, sclabel);
						sceneList.clear();
						if(init_Scenes()) {
							SetAlarm("Update", SOCKET_COLLECTION_TIMEOUT, true);
							message["text"] = "Scene created with name " + sclabel +" and scene_id " + ssID.str();
						}
						else {
							message["text"] = "Scene created, but scenelist could not be refreshed"; //create better error message
						}
					}
					Manager::Get()->WriteConfig(g_homeId);
					break;
				}
				case Add:
				{
					if(v.size() != 5) {
						throw OZWSS::ProtocolException("Wrong number of arguments", 2);
					}
					uint8 numscenes = 0;
					uint8 *sceneIds = new uint8[numscenes];

					if((numscenes = Manager::Get()->GetAllScenes(&sceneIds))==0) {
						throw OZWSS::ProtocolException("No scenes created", 3);
					}

					string sclabel = trim(v[2]);
					int scid=0;
					int Node = lexical_cast<int>(v[3]);
					double value = lexical_cast<double>(v[4]);
					bool response;

					for(int i=0; i<numscenes; ++i) {
						scid = sceneIds[i];
						if(sclabel != Manager::Get()->GetSceneLabel(scid)) {
							continue;
						}
						NodeInfo* nodeInfo = GetNodeInfo(g_homeId, Node);
						uint8 cmdclass = 0;
						if(nodeInfo->m_basicmapping > 0 || try_map_basic(g_homeId, Node)) {
							cmdclass = nodeInfo->m_basicmapping;
							std::cout << "mapped to " << (int) cmdclass << endl;
						}
						else {
							cmdclass = COMMAND_CLASS_BASIC;
							std::cout << "mapped to BASIC" << endl;
						}
						for(list<ValueID>::iterator vit = nodeInfo->m_values.begin(); vit != nodeInfo->m_values.end(); ++vit) {
							if((*vit).GetCommandClassId() == cmdclass) {
								// It works fine, EXCEPT for MULTILEVEL, then we need to ignore all except the first one
								if((*vit).GetCommandClassId() == COMMAND_CLASS_SWITCH_MULTILEVEL) {
									if((*vit).GetIndex() != 0) {
										continue;
									}
								}

								switch((*vit).GetType()) {
									case ValueID::ValueType_Bool: {
										bool bool_value;
										bool_value = (bool)value;
										response = Manager::Get()->AddSceneValue(scid, (*vit), bool_value);
										break;
									}
									case ValueID::ValueType_Byte: {
										uint8 uint8_value;
										uint8_value = (uint8)value;
										response = Manager::Get()->AddSceneValue(scid, (*vit), uint8_value);
										break;
									}
									case ValueID::ValueType_Short: {
										uint16 uint16_value;
										uint16_value = (uint16)value;
										response = Manager::Get()->AddSceneValue(scid, (*vit), uint16_value);
										break;
									}
									case ValueID::ValueType_Int: {
										int int_value;
										int_value = value;
										response = Manager::Get()->AddSceneValue(scid, (*vit), int_value);
										break;
									}
									case ValueID::ValueType_Decimal: {
										float float_value;
										float_value = (float)value;
										response = Manager::Get()->AddSceneValue(scid, (*vit), float_value);
										break;
									}
									case ValueID::ValueType_List: {
										response = Manager::Get()->AddSceneValue(scid, (*vit), (int)value);
										break;
									}
									default:
										response = false;
										message["error"]["err_message"] = "unknown ValueType";
										break;
								}

								if(!response) {
									message["error"]["err_main"] = "Could not add valueid/value to scene " + sclabel + "\nPlease send me an issue at Github";
								} else {
									message["text"] = "Added valueid/value to scene " + sclabel;
								}
							}
						}
					}
					delete sceneIds;
					Manager::Get()->WriteConfig(g_homeId);
					break;
				}
				case Remove:
				{
					if(v.size() != 4) {
						throw OZWSS::ProtocolException("Wrong number of arguments", 2);
					}
					uint8 numscenes = 0;
					uint8 *sceneIds = new uint8[numscenes];

					if((numscenes = Manager::Get()->GetAllScenes(&sceneIds))==0) {
						throw OZWSS::ProtocolException("No scenes created", 3);
					}

					string sclabel = trim(v[2]);
					int scid=0;
					int Node = lexical_cast<int>(v[3]);

					for(int i=0; i<numscenes; ++i){
						scid = sceneIds[i];

						if(sclabel != Manager::Get()->GetSceneLabel(scid)){
							continue;
						}
						NodeInfo* nodeInfo = GetNodeInfo(g_homeId, Node);
						uint8 cmdclass = 0;
						if(nodeInfo->m_basicmapping > 0 || try_map_basic(g_homeId, Node)) {
							cmdclass = nodeInfo->m_basicmapping;
							std::cout << "mapped to " << (int) cmdclass << endl;
						}
						else {
							cmdclass = COMMAND_CLASS_BASIC;
							std::cout << "mapped to BASIC" << endl;
						}
						for(list<ValueID>::iterator vit = nodeInfo->m_values.begin(); vit != nodeInfo->m_values.end(); ++vit) {
							if((*vit).GetCommandClassId() == cmdclass) {
								// It works fine, EXCEPT for MULTILEVEL, then we need to ignore all except the first one
								if((*vit).GetCommandClassId() == COMMAND_CLASS_SWITCH_MULTILEVEL) {
									if((*vit).GetIndex() != 0) {
										continue;
									}
								}
								Manager::Get()->RemoveSceneValue(scid, (*vit));
								message["text"] = "Removed valueid from scene" + sclabel;
							}
						}
					}
					delete sceneIds;
					Manager::Get()->WriteConfig(g_homeId);
					break;
				}
				case Activate:
				{
					message["text"] = activateScene(v[2]);
					break;
				}
				default:
					throw OZWSS::ProtocolException("Unknown Scene command", 1);
					break;
			}
			break;
		}
		case ControllerC:
		{
			switch(s_mapStringCommands[trim(v[1])])
			{
				case Add: {
					if(v.size() != 3) {
						throw OZWSS::ProtocolException("Wrong number of arguments", 2);
					}
					if(Manager::Get()->AddNode(g_homeId, lexical_cast<bool>(v[2]))) {
						message["text"] = "Controller is now in inclusion mode, see the server console for more information";
					} else {
						message["error"]["err_main"] = "Controller could not be set to inclusion mode, see the server console for more information";
					}
					break;
				}
				case Remove: {
					if(Manager::Get()->RemoveNode(g_homeId)) {
						message["text"] = "Controller is now in exclusion mode, see the server console for more information";
					} else {
						message["error"]["err_main"] = "Controller could not be set to exclusion mode, see the server console for more information";
					}
					break;
				}
				case Cancel: {
					if(Manager::Get()->CancelControllerCommand(g_homeId)) {
						message["text"] = "Controller is now back to normal functioning";
					} else {
						message["error"]["err_main"] = "Controller is stuck in inclusion/exclusion mode, please check the server console";
					}
					break;
				}
				case Reset: {
					Manager::Get()->ResetController(g_homeId);
					message["text"] = "The controller has been reset";
					break;
				}
				default:
					break;
			}
		}
		case Cron:
		{
			//planning to add a google calendar add-in here.

			//set the daily alarms and check if any have passed already (I execute zcron.sh around 4:15 AM)
			time_t sunrise = 0, sunset = 0;
			float lat, lon;
			conf->GetLocation(lat, lon);
			if(OZWSS::GetSunriseSunset(sunrise,sunset,lat,lon)) {
				SetAlarm("Sunrise", sunrise, false);
				SetAlarm("Sunset", sunset, false);
			}

			//synchronize devices with Command_Class_Clock
			for(list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it) {
				time_t rawtime;
				tm * timeinfo;
				time(&rawtime);
				timeinfo=localtime(&rawtime);

				for(list<ValueID>::iterator vit = (*it)->m_values.begin(); vit != (*it)->m_values.end(); ++vit) {
					if((*vit).GetCommandClassId() != COMMAND_CLASS_CLOCK) {
						continue;
					}

					switch((*vit).GetIndex()) {
						case 0: {
							std::string deviceDayValue;
							const std::string DAY[]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
							if(Manager::Get()->GetValueListSelection((*vit), &deviceDayValue)) {
								if(strcmp(DAY[(timeinfo->tm_wday)].c_str(), deviceDayValue.c_str()) != 0) {
									(*it)->m_needsSync = true;
								}
							}
							else {
								stringstream ssNodeId;
								ssNodeId << (*it)->m_nodeId;
								message["error"]["err_message"].append("Could not get the day out of node " + ssNodeId.str());
							}
							break;
						}
						case 1: {
							uint8 deviceHourValue = -1;
							if(Manager::Get()->GetValueAsByte((*vit), &deviceHourValue)) {
								if((int)deviceHourValue != timeinfo->tm_hour) {
									(*it)->m_needsSync = true;
								}
							}
							else {
								stringstream ssNodeId;
								ssNodeId << (*it)->m_nodeId;
								message["error"]["err_message"].append("Could not get the hour out of node " + ssNodeId.str());
							}
							break;
						}
						case 2: {
							uint8 deviceMinuteValue = -1;
							if(Manager::Get()->GetValueAsByte((*vit), &deviceMinuteValue)) {
								if((int)deviceMinuteValue != timeinfo->tm_min) {
									(*it)->m_needsSync = true;
								}
							}
							else {
								stringstream ssNodeId;
								ssNodeId << (*it)->m_nodeId;
								message["error"]["err_message"].append("Could not get the minute out of node " + ssNodeId.str());
							}
							break;
						}
						default:
							stringstream ssNodeId;
							ssNodeId << (*it)->m_nodeId;
							message["error"]["err_main"] = "Could not read the time from node " + ssNodeId.str();
							break;
					}
				}
				//cout << (*it)->m_needsSync << endl;
			}
			break;
		}
		case Switch:
		{
			message["text"] = switchAtHome();
			SetAlarm("Update", SOCKET_COLLECTION_TIMEOUT, true);
			break;
		}
		case AtHome:
		{
			if(atHome) {
				message["athome"] = true;
			}
			else {
				message["athome"] = false;
			}
			break;
		}
		case PollInterval:
		{
			if(v.size() != 2) {
				throw OZWSS::ProtocolException("Wrong number of arguments", 2);
			}
			int interval = lexical_cast<int>(v[1]); //get the interval in minutes
			Manager::Get()->SetPollInterval(1000*60*interval, false);
			message["text"] = "Set poll interval to " + v[1] + " minutes";
		}
		case AlarmList:
		{
			int alarmpos = 0;
			for(list<Alarm>::iterator ait = alarmList.begin(); ait!=alarmList.end(); ait++) {
				message["alarms"][alarmpos]["description"] = ait->description;
				message["alarms"][alarmpos]["time"] = trim(ctime(&(ait->alarmtime)));
				++alarmpos;
			}
			break;
		}
		case Test:
		{
			break;
		}
		case Exit:
			stopping = true;
			break;
		default:
			throw OZWSS::ProtocolException("Unknown command", 1);
			break;
	}
}

//-----------------------------------------------------------------------------
// parse_option
// Parse options for the SETNODE command
//-----------------------------------------------------------------------------
bool parse_option(int32 home, int32 node, std::string name, std::string value, bool& save, std::string& err_message) {
	err_message = "";
	switch(s_mapStringOptions[name])
	{
		case Name:
		{
			pthread_mutex_lock(&g_criticalSection);
			Manager::Get()->SetNodeName(home, node, value);
			pthread_mutex_unlock(&g_criticalSection);
			save = true;
			return true;
			break;
		}
		case Location:
		{
			pthread_mutex_lock(&g_criticalSection);
			Manager::Get()->SetNodeLocation(home, node, value);
			pthread_mutex_unlock(&g_criticalSection);
			save = true;
			roomList.clear();
			return init_Rooms(); //can do this more efficiently, patch welcome
			break;
		}
		case SwitchC:
		{
			uint8 cmdclass = COMMAND_CLASS_SWITCH_BINARY;
			return SetValue(home, node, value, cmdclass, "Switch", err_message);
			break;
		}
		case Level:
		{
			uint8 cmdclass = COMMAND_CLASS_SWITCH_MULTILEVEL;
			return SetValue(home, node, value, cmdclass, "Level", err_message);
			break;
		}
		case Thermostat_Setpoint: {
			uint8 cmdclass = COMMAND_CLASS_THERMOSTAT_SETPOINT;
			return SetValue(home, node, value, cmdclass, "Heating 1", err_message);
			break;
		}
		case Polling:
		{
			bool found = false;
			if(Manager::Get()->GetNodeBasic( home, node ) < 0x03) {
				err_message += "Node is a controller\n";
				return false;
			}
			if(NodeInfo* nodeInfo = GetNodeInfo(home, node)) {
				uint8 cmdclass = 0;
				if(nodeInfo->m_basicmapping > 0 || try_map_basic(home, node)) {
					cmdclass = nodeInfo->m_basicmapping;
					std::cout << "mapped to " << (int) cmdclass << endl;
				}
				else {
					cmdclass = COMMAND_CLASS_BASIC;
					std::cout << "mapped to BASIC" << endl;
				}

				// Mark the basic command class values for polling
				for(list<ValueID>::iterator it = nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it){
					if((*it).GetCommandClassId() == cmdclass) {
						// It works fine, EXCEPT for MULTILEVEL, then we need to ignore all except the first one
						if((*it).GetCommandClassId() == COMMAND_CLASS_SWITCH_MULTILEVEL) {
							if((*it).GetIndex() != 0) {
								continue;
							}
						}
						if(lexical_cast<int>(value) == 1) {
							if(!Manager::Get()->EnablePoll(*it)) {
								err_message += "Could not enable polling for this value\n";
								return false;
							}
						}
						else if(lexical_cast<int>(value) >= 2) {
							if(!Manager::Get()->EnablePoll(*it, 2)) {
								err_message += "Could not enable polling for this value\n";
								return false;
							}
						}
						else {
							if(!Manager::Get()->DisablePoll(*it)) {
								err_message += "Could not disable polling for this value\n";
								return false;
							}
						}
						if(!found)
						{
							found = true;
						}
					}
				}
				if(!found) {
					err_message += "Node does not have COMMAND_CLASS_BASIC\n";
					return false;
				}
			}
			save = true;
			return true;
			break;
		}
		case Wake_up_Interval:
		{
			stringstream key;
			key << (int) home << (int) node;

			if(WakeupIntervalCache.count(key.str()) == 0)
			{
				std::cout << key.str() << endl;
				err_message += "This device does not have a wake-up interval\n";
				return false;
			}
			else
			{
				WakeupIntervalCacheItem cacheItem = WakeupIntervalCache[key.str()];
				int newInterval = lexical_cast<int>(value);
				if(cacheItem.maxInterval < newInterval || cacheItem.minInterval > newInterval)
				{
					err_message += "The new interval is not within bounds of min and max interval for this device\n";
					return false;
				}
				else
				{
					cacheItem.interval = newInterval;
					WakeupIntervalCache[key.str()] = cacheItem;
					uint8 cmdclass = COMMAND_CLASS_WAKE_UP;
					save = true;
					return SetValue(home, node, value, cmdclass, name, err_message);
				}
			}
		}
		case Battery_report:
		{
			uint8 cmdclass = COMMAND_CLASS_CONFIGURATION;
			save = true;
			return SetValue(home, node, value, cmdclass, "Send unsolicited battery report on wakeup", err_message);
		}
		default:
			break;
	}
	return false;
}

//-----------------------------------------------------------------------------
// SetValue
// set a certain value for a node in the open-zwave network
//-----------------------------------------------------------------------------
bool SetValue(int32 home, int32 node, std::string const value, uint8 cmdclass, std::string label, std::string& err_message) {
	err_message = "";
	bool response;
	bool cmdfound = false;

	if(NodeInfo* nodeInfo = GetNodeInfo(home, node)) {
		// Find the correct instance
		for(list<ValueID>::iterator it = nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it) {
			int id = (*it).GetCommandClassId();
			if(id != cmdclass) {
				continue;
			}

			if(label != Manager::Get()->GetValueLabel(*it)) {
				continue;
			}

			switch((*it).GetType()) {
				case ValueID::ValueType_Bool: {
					response = Manager::Get()->SetValue(*it, lexical_cast<bool>(value));
					cmdfound = true;
					break;
				}
				case ValueID::ValueType_Byte: {
					response = Manager::Get()->SetValue(*it, (uint8) lexical_cast<int>(value));
					cmdfound = true;
					break;
				}
				case ValueID::ValueType_Short: {
					response = Manager::Get()->SetValue(*it, (uint16) lexical_cast<int>(value));
					cmdfound = true;
					break;
				}
				case ValueID::ValueType_Int: {
					response = Manager::Get()->SetValue(*it, lexical_cast<int>(value));
					cmdfound = true;
					break;
				}
				case ValueID::ValueType_Decimal: {
					response = Manager::Get()->SetValue(*it, lexical_cast<float>(value));
					cmdfound = true;
					break;
				}
				case ValueID::ValueType_List: {
					response = Manager::Get()->SetValueListSelection(*it, value);
					cmdfound = true;
					break;
				}
				default:
					err_message += "unknown ValueType | ";
					return false;
					break;
			}
		}

		if(!cmdfound) {
			stringstream ssnode;
			ssnode << node;
			err_message += "Could not match node " + ssnode.str() + " to the required command class\n";
			return false;
		}
	}
	else {
		//WriteLog( LogLevel_Debug, false, "Return=false (node doesn't exist)" );
		err_message += "node doesn't exist\n";
		response = false;
	}

	return response;
}

//-----------------------------------------------------------------------------
// activateScene
// Try to activate a scene by its label/name
//-----------------------------------------------------------------------------
std::string activateScene(std::string sclabel) {
	uint8 numscenes = 0;
	uint8 *sceneIds = new uint8[numscenes];

	sclabel = trim(sclabel);

	if((numscenes = Manager::Get()->GetAllScenes(&sceneIds))==0) {
		throw OZWSS::ProtocolException("No scenes created", 3);
	}

	int scid=0;

	for(int i=0; i<numscenes; ++i) {
		scid = sceneIds[i];
		if(sclabel != Manager::Get()->GetSceneLabel(scid)){
			continue;
		}
		delete sceneIds;
		Manager::Get()->ActivateScene(scid);
		return "Activate scene "+sclabel;
	}
	delete sceneIds;
	throw OZWSS::ProtocolException("Scene not found", 4);
}

//-----------------------------------------------------------------------------
// switchAtHome
// Switch between the at home or away state
// Activate the proper scene for the current state after switching
//-----------------------------------------------------------------------------
std::string switchAtHome() {
	time_t sunrise = 0, sunset = 0;
	float lat, lon;
	std::string output = "";
	if(!conf->GetLocation(lat, lon)) {
		output += "Could not get the location from Config.ini\n";
		return output;
	}
	if(OZWSS::GetSunriseSunset(sunrise,sunset,lat,lon)) {
		atHome = !atHome;
		if(atHome) {
			output += "Welcome home\n";
			time_t now = time(NULL);
			if(now > sunrise && now < sunset) {
				// turn on the athome scene set by the user for the day
				std::string dayScene;
				conf->GetDayScene(dayScene);
				try {
					output += activateScene(dayScene);
				}
				catch (OZWSS::ProtocolException& e) {
					std::string what = "ProtocolException: ";
					what += e.what();
					output += what + "\n";
					output += "No dayScene is set, set it in Config.ini\n";
				}
			}
			else {
				// turn on the athome scene set by the user for the evening/night
				std::string nightScene;
				conf->GetNightScene(nightScene);
				try {
					output += activateScene(nightScene);
				}
				catch (OZWSS::ProtocolException& e) {
					std::string what = "ProtocolException: ";
					what += e.what();
					output += what + "\n";
					output += "No nightScene is set, set it in Config.ini\n";
				}
			}
		}
		else {
			output += "Bye bye\n";
			std::string awayScene;
			conf->GetAwayScene(awayScene);
			try {
				output += activateScene(awayScene);
			}
			catch (OZWSS::ProtocolException& e) {
				string what = "ProtocolException: ";
				what += e.what();
				output += what + "\n";
				output += "No awayScene is set, set it in Config.ini\n";
			}
		}
	}
	return output;
}

//-----------------------------------------------------------------------------
// try_map_basic
// Try to map the basic command class for a node to the proper command class
//-----------------------------------------------------------------------------
bool try_map_basic(int32 home, int32 node) {
	char buffer[10];
	if(NodeInfo* nodeInfo = GetNodeInfo(home, node)) {
		uint8 generic = Manager::Get()->GetNodeGeneric(home, node);
		uint8 specific = Manager::Get()->GetNodeSpecific(home, node);

		snprintf(buffer, 10, "0x%02X|0x%02X", generic, specific);
		if(MapCommandClassBasic.find(buffer) != MapCommandClassBasic.end()) {
			nodeInfo->m_basicmapping = MapCommandClassBasic[buffer];
			return true;
		}
		else {
			// We didn't find a Generic+Specifc in the table, now we check
			// for Generic only
			snprintf(buffer, 10, "0x%02X", generic);

			// Check if we have a mapping in our map table
			if(MapCommandClassBasic.find(buffer) != MapCommandClassBasic.end()) {
				nodeInfo->m_basicmapping = MapCommandClassBasic[buffer];
				return true;
			}
		}
	}
	return false;
}

void SetAlarm(std::string description, time_t alarmtime, bool offset) {
	time_t now = time(NULL);
	Alarm newAlarm;
	newAlarm.description = description;
	if(offset) {
		newAlarm.alarmtime = now+alarmtime;
	} else {
		newAlarm.alarmtime = alarmtime;
	}

	while(!alarmList.empty() && (alarmList.front().alarmtime) <= now)
		{alarmList.pop_front();}

	alarmList.push_back(newAlarm);
	alarmList.sort();
	alarmList.unique();

	signal(SIGALRM, sigalrm_handler);
	alarm(alarmList.front().alarmtime - now);
}

//-----------------------------------------------------------------------------
// sigalrm_handler
// Gets invoked when a Alarm goes off
// take apropriate action for the type of Alarm
//-----------------------------------------------------------------------------
void sigalrm_handler(int sig) {
	alarmset = false;
	Alarm currentAlarm = alarmList.front();
	alarmList.pop_front();

	switch(s_mapStringTriggers[currentAlarm.description])
	{
		case Sunrise:
		{
			if(atHome) {
				std::string morningScene;
				conf->GetMorningScene(morningScene);
				try {
					std::cout << activateScene(morningScene);
				}
				catch (OZWSS::ProtocolException& e) {
					string what = "ProtocolException: ";
					what += e.what();
					std::cout << what << endl;
					std::cout << "trigger went off, but no Scene is set for Sunrise" << endl;
				}
			}
			break;
		}
		case Sunset:
		{
			if(atHome) {
				std::string nightScene;
				conf->GetNightScene(nightScene);
				try {
					std::cout << activateScene(nightScene);
				}
				catch (OZWSS::ProtocolException& e) {
					string what = "ProtocolException: ";
					what += e.what();
					std::cout << what << endl;
					std::cout << "trigger went off, but no Scene is set for Sunset" << endl;
				}
			}
			break;
		}
		case Thermostat:
		{
			for(list<Room>::iterator rit = roomList.begin(); rit != roomList.end(); ++rit) {
				std::cout << "sending commands for room " << rit->name << endl;
				if(rit->changed) {
					for(list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it) {
						if(strcmp(rit->name.c_str(), Manager::Get()->GetNodeLocation(g_homeId, (*it)->m_nodeId).c_str()) != 0) {
							continue;
						}
						if(strcmp(Manager::Get()->GetNodeType(g_homeId, (*it)->m_nodeId).c_str(), "Setpoint Thermostat") !=0) {
							continue;
						}
						stringstream ssCurrentSetpoint;
						ssCurrentSetpoint << rit->setpoint;
						uint8 cmdclass = COMMAND_CLASS_THERMOSTAT_SETPOINT;
						string err_message = "";
						if(!SetValue(g_homeId, (*it)->m_nodeId, ssCurrentSetpoint.str(), cmdclass, "Heating 1", err_message)) {
							std::cout << err_message;
						}
					}
				}
			}
			break;
		}
		case Update:
		{
			std::cout << "Adding notification to message list" << endl;

			Json::Value message;
			message["command"] = "UPDATE";
			//add individual updates for devices or scenes later

			LWSMessage lwsmessage;
			Json::FastWriter fastWriter;
			lwsmessage.message = fastWriter.write(message);
			lwsmessage.broadcast = true;

			ringbuffer[ringbuffer_head] = lwsmessage;

			if (ringbuffer_head == (MAX_MESSAGE_QUEUE - 1)) {
				ringbuffer_head = 0;
			}
			else {
				ringbuffer_head++;
			}

			lws_callback_on_writable_all_protocol(context, protocols+1);
			break;
		}
		case Cache_init:
		{
			WakeupIntervalCache.clear();
			init_WakeupIntervalCache();
			break;
		}
		default:
			std::cout << "wrong alarm description";
			// check if the description can be used as a Scene name
			// if that fails, check if the description can be parsed as a command
		break;
	}

	// more alarms on the alarmList? Set the next one (the list is already sorted...)
	time_t now = time(NULL);
	// remove alarms that are set in the past...
	while(!alarmList.empty() && (alarmList.front().alarmtime) <= now)
	{alarmList.pop_front();}
	// set the next alarm
	if(!alarmList.empty() && !alarmset && (alarmList.front().alarmtime > now)) {
		alarm((alarmList.front().alarmtime - now));
		alarmset = true;
	}
}
